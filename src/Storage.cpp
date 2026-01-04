#include "Storage.h"
#include "esp_log.h"

static const char* TAG = "Storage";

// Global instance
StorageManager storageManager;

//=============================================================================
// StorageManager Implementation
//=============================================================================

StorageManager::StorageManager()
    : _initialized(false) {
}

StorageManager::~StorageManager() {
    end();
}

bool StorageManager::begin() {
    if (_initialized) return true;

    ESP_LOGI(TAG, "Initializing storage");
    _initialized = true;
    return true;
}

void StorageManager::end() {
    if (_initialized) {
        _prefs.end();
        _initialized = false;
    }
}

bool StorageManager::saveRoutings(const std::vector<MidiRouting>& routings) {
    if (!_initialized) return false;

    _prefs.begin(NS_ROUTINGS, false);

    // Save count
    _prefs.putUInt(KEY_ROUTING_COUNT, routings.size());

    // Save each routing
    for (size_t i = 0; i < routings.size(); i++) {
        String key = String(KEY_ROUTING_PREFIX) + String(i);
        String data = routings[i].serialize();
        _prefs.putString(key.c_str(), data);
    }

    // Clear any old entries beyond current count
    for (size_t i = routings.size(); i < MAX_ROUTINGS; i++) {
        String key = String(KEY_ROUTING_PREFIX) + String(i);
        if (_prefs.isKey(key.c_str())) {
            _prefs.remove(key.c_str());
        } else {
            break;  // No more old entries
        }
    }

    _prefs.end();

    ESP_LOGI(TAG, "Saved %d routings", routings.size());
    return true;
}

bool StorageManager::loadRoutings(std::vector<MidiRouting>& routings) {
    if (!_initialized) return false;

    routings.clear();

    _prefs.begin(NS_ROUTINGS, true);  // Read-only

    size_t count = _prefs.getUInt(KEY_ROUTING_COUNT, 0);

    for (size_t i = 0; i < count && i < MAX_ROUTINGS; i++) {
        String key = String(KEY_ROUTING_PREFIX) + String(i);
        String data = _prefs.getString(key.c_str(), "");

        if (data.length() > 0) {
            MidiRouting routing = MidiRouting::deserialize(data);
            if (routing.getId() > 0) {
                routings.push_back(routing);
            }
        }
    }

    _prefs.end();

    ESP_LOGI(TAG, "Loaded %d routings", routings.size());
    return true;
}

bool StorageManager::clearRoutings() {
    if (!_initialized) return false;

    _prefs.begin(NS_ROUTINGS, false);
    _prefs.clear();
    _prefs.end();

    ESP_LOGI(TAG, "Cleared all routings");
    return true;
}

bool StorageManager::saveDeviceName(const String& uniqueId, const String& name) {
    if (!_initialized) return false;

    _prefs.begin(NS_DEVICES, false);

    // We need to store device mappings
    // Use a simple scheme: count + indexed entries
    size_t count = _prefs.getUInt(KEY_DEVICE_COUNT, 0);

    // Check if this device already exists
    bool found = false;
    for (size_t i = 0; i < count; i++) {
        String idKey = String(KEY_DEVICE_PREFIX) + String(i) + "_id";
        String existingId = _prefs.getString(idKey.c_str(), "");
        if (existingId == uniqueId) {
            // Update existing entry
            String nameKey = String(KEY_DEVICE_PREFIX) + String(i) + "_name";
            _prefs.putString(nameKey.c_str(), name);
            found = true;
            break;
        }
    }

    if (!found) {
        // Add new entry
        String idKey = String(KEY_DEVICE_PREFIX) + String(count) + "_id";
        String nameKey = String(KEY_DEVICE_PREFIX) + String(count) + "_name";
        _prefs.putString(idKey.c_str(), uniqueId);
        _prefs.putString(nameKey.c_str(), name);
        _prefs.putUInt(KEY_DEVICE_COUNT, count + 1);
    }

    _prefs.end();
    return true;
}

String StorageManager::loadDeviceName(const String& uniqueId) {
    if (!_initialized) return "";

    _prefs.begin(NS_DEVICES, true);

    size_t count = _prefs.getUInt(KEY_DEVICE_COUNT, 0);
    String result = "";

    for (size_t i = 0; i < count; i++) {
        String idKey = String(KEY_DEVICE_PREFIX) + String(i) + "_id";
        String existingId = _prefs.getString(idKey.c_str(), "");
        if (existingId == uniqueId) {
            String nameKey = String(KEY_DEVICE_PREFIX) + String(i) + "_name";
            result = _prefs.getString(nameKey.c_str(), "");
            break;
        }
    }

    _prefs.end();
    return result;
}

bool StorageManager::loadAllDeviceNames(std::vector<std::pair<String, String>>& devices) {
    if (!_initialized) return false;

    devices.clear();

    _prefs.begin(NS_DEVICES, true);

    size_t count = _prefs.getUInt(KEY_DEVICE_COUNT, 0);

    for (size_t i = 0; i < count; i++) {
        String idKey = String(KEY_DEVICE_PREFIX) + String(i) + "_id";
        String nameKey = String(KEY_DEVICE_PREFIX) + String(i) + "_name";

        String id = _prefs.getString(idKey.c_str(), "");
        String name = _prefs.getString(nameKey.c_str(), "");

        if (id.length() > 0) {
            devices.push_back({id, name});
        }
    }

    _prefs.end();
    return true;
}

bool StorageManager::saveSettings(uint8_t brightness, bool soundEnabled) {
    if (!_initialized) return false;

    _prefs.begin(NS_SETTINGS, false);
    _prefs.putUChar(KEY_BRIGHTNESS, brightness);
    _prefs.putBool(KEY_SOUND, soundEnabled);
    _prefs.end();

    return true;
}

bool StorageManager::loadSettings(uint8_t& brightness, bool& soundEnabled) {
    if (!_initialized) return false;

    _prefs.begin(NS_SETTINGS, true);
    brightness = _prefs.getUChar(KEY_BRIGHTNESS, 128);
    soundEnabled = _prefs.getBool(KEY_SOUND, true);
    _prefs.end();

    return true;
}

size_t StorageManager::getFreeSpace() {
    // Preferences doesn't have a direct way to get free space
    // Return a placeholder value
    return 0;
}

size_t StorageManager::getUsedSpace() {
    // Preferences doesn't have a direct way to get used space
    // Return a placeholder value
    return 0;
}
