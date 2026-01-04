#include "Routing.h"
#include "Storage.h"
#include "esp_log.h"

static const char* TAG = "Routing";

// Global instance
RoutingManager routingManager;

//=============================================================================
// MidiRouting Implementation
//=============================================================================

MidiRouting::MidiRouting()
    : _id(0), _enabled(true), _messageCount(0) {
}

MidiRouting::MidiRouting(uint8_t id, const String& srcDeviceId, const String& dstDeviceId)
    : _id(id), _srcDeviceId(srcDeviceId), _dstDeviceId(dstDeviceId),
      _enabled(true), _messageCount(0) {
}

bool MidiRouting::isActive() const {
    if (!_enabled) return false;

    // Check if both source and destination devices are connected
    MidiDevice* srcDevice = deviceManager.findDevice(_srcDeviceId);
    MidiDevice* dstDevice = deviceManager.findDevice(_dstDeviceId);

    return (srcDevice && srcDevice->isConnected() &&
            dstDevice && dstDevice->isConnected());
}

bool MidiRouting::shouldRoute(const MidiMessage& msg) const {
    if (!_enabled) return false;

    // System messages always pass through
    if (!msg.isChannelMessage()) return true;

    // Check channel filter
    return _channelFilter.isChannelEnabled(msg.channel);
}

String MidiRouting::serialize() const {
    // Format: id|srcId|dstId|channelMask|enabled
    char buf[256];
    snprintf(buf, sizeof(buf), "%d|%s|%s|%04X|%d",
             _id,
             _srcDeviceId.c_str(),
             _dstDeviceId.c_str(),
             _channelFilter.channelMask,
             _enabled ? 1 : 0);
    return String(buf);
}

MidiRouting MidiRouting::deserialize(const String& data) {
    MidiRouting routing;

    // Parse the serialized format
    int idx = 0;
    int prevIdx = 0;
    String parts[5];
    int partCount = 0;

    for (size_t i = 0; i <= data.length() && partCount < 5; i++) {
        if (i == data.length() || data[i] == '|') {
            parts[partCount++] = data.substring(prevIdx, i);
            prevIdx = i + 1;
        }
    }

    if (partCount >= 5) {
        routing._id = parts[0].toInt();
        routing._srcDeviceId = parts[1];
        routing._dstDeviceId = parts[2];
        routing._channelFilter.channelMask = strtol(parts[3].c_str(), nullptr, 16);
        routing._enabled = parts[4].toInt() != 0;
    }

    return routing;
}

//=============================================================================
// RoutingManager Implementation
//=============================================================================

RoutingManager::RoutingManager()
    : _nextRoutingId(1), _totalMessagesRouted(0) {
}

RoutingManager::~RoutingManager() {
}

void RoutingManager::begin() {
    // Load saved routings from storage
    loadRoutings();
    ESP_LOGI(TAG, "Routing manager initialized with %d routings", _routings.size());
}

MidiRouting* RoutingManager::addRouting(const String& srcDeviceId, const String& dstDeviceId) {
    if (_routings.size() >= MAX_ROUTINGS) {
        ESP_LOGW(TAG, "Maximum routings reached");
        return nullptr;
    }

    // Check for duplicate routing
    for (const auto& routing : _routings) {
        if (routing.getSourceDeviceId() == srcDeviceId &&
            routing.getDestDeviceId() == dstDeviceId) {
            ESP_LOGW(TAG, "Duplicate routing exists");
            return nullptr;
        }
    }

    MidiRouting routing(_nextRoutingId++, srcDeviceId, dstDeviceId);
    _routings.push_back(routing);

    ESP_LOGI(TAG, "Added routing %d: %s -> %s",
             routing.getId(), srcDeviceId.c_str(), dstDeviceId.c_str());

    // Save to persistent storage
    saveRoutings();

    return &_routings.back();
}

bool RoutingManager::removeRouting(uint8_t routingId) {
    for (auto it = _routings.begin(); it != _routings.end(); ++it) {
        if (it->getId() == routingId) {
            ESP_LOGI(TAG, "Removed routing %d", routingId);
            _routings.erase(it);
            saveRoutings();
            return true;
        }
    }
    return false;
}

MidiRouting* RoutingManager::getRouting(uint8_t id) {
    for (auto& routing : _routings) {
        if (routing.getId() == id) {
            return &routing;
        }
    }
    return nullptr;
}

MidiRouting* RoutingManager::getRoutingByIndex(size_t index) {
    if (index < _routings.size()) {
        return &_routings[index];
    }
    return nullptr;
}

void RoutingManager::routeMidiMessage(uint8_t srcDeviceId, const MidiMessage& msg) {
    // Find the source device's unique ID
    MidiDevice* srcDevice = deviceManager.getDevice(srcDeviceId);
    if (!srcDevice) return;

    String srcUniqueId = srcDevice->getUniqueId();

    // Find all routings from this device
    for (auto& routing : _routings) {
        if (routing.getSourceDeviceId() != srcUniqueId) continue;
        if (!routing.shouldRoute(msg)) continue;

        // Find destination device
        MidiDevice* dstDevice = deviceManager.findDevice(routing.getDestDeviceId());
        if (!dstDevice || !dstDevice->isConnected()) continue;

        // Send the MIDI message
        if (deviceManager.sendMidi(dstDevice->getId(), msg)) {
            routing.setLastMessage(msg);
            _totalMessagesRouted++;

            ESP_LOGD(TAG, "Routed: %s -> %s [%02X %02X %02X]",
                     srcDevice->getName().c_str(),
                     dstDevice->getName().c_str(),
                     msg.status, msg.data1, msg.data2);
        }
    }
}

bool RoutingManager::isRoutingActive(uint8_t routingId) const {
    for (const auto& routing : _routings) {
        if (routing.getId() == routingId) {
            return routing.isActive();
        }
    }
    return false;
}

void RoutingManager::clearAll() {
    _routings.clear();
    _nextRoutingId = 1;
    saveRoutings();
    ESP_LOGI(TAG, "All routings cleared");
}

bool RoutingManager::saveRoutings() {
    return storageManager.saveRoutings(_routings);
}

bool RoutingManager::loadRoutings() {
    bool result = storageManager.loadRoutings(_routings);

    // Find the highest routing ID to set next ID
    _nextRoutingId = 1;
    for (const auto& routing : _routings) {
        if (routing.getId() >= _nextRoutingId) {
            _nextRoutingId = routing.getId() + 1;
        }
    }

    return result;
}

std::vector<MidiRouting*> RoutingManager::findRoutingsFromDevice(const String& deviceUniqueId) {
    std::vector<MidiRouting*> result;
    for (auto& routing : _routings) {
        if (routing.getSourceDeviceId() == deviceUniqueId) {
            result.push_back(&routing);
        }
    }
    return result;
}
