#ifndef ROUTING_H
#define ROUTING_H

#include "MidiTypes.h"
#include "MidiDevice.h"

// A single MIDI routing configuration
class MidiRouting {
public:
    MidiRouting();
    MidiRouting(uint8_t id, const String& srcDeviceId, const String& dstDeviceId);

    // Identification
    uint8_t getId() const { return _id; }
    void setId(uint8_t id) { _id = id; }

    // Source and destination device IDs (unique identifiers, not runtime IDs)
    String getSourceDeviceId() const { return _srcDeviceId; }
    String getDestDeviceId() const { return _dstDeviceId; }
    void setSourceDeviceId(const String& id) { _srcDeviceId = id; }
    void setDestDeviceId(const String& id) { _dstDeviceId = id; }

    // Channel filter
    ChannelFilter& getChannelFilter() { return _channelFilter; }
    const ChannelFilter& getChannelFilter() const { return _channelFilter; }
    void setChannelFilter(const ChannelFilter& filter) { _channelFilter = filter; }

    // Enable/disable
    bool isEnabled() const { return _enabled; }
    void setEnabled(bool enabled) { _enabled = enabled; }

    // Check if routing is currently active (both devices connected)
    bool isActive() const;

    // Process a MIDI message - returns true if message should be routed
    bool shouldRoute(const MidiMessage& msg) const;

    // Statistics
    uint32_t getMessageCount() const { return _messageCount; }
    void incrementMessageCount() { _messageCount++; }
    void resetMessageCount() { _messageCount = 0; }

    // Last message that passed through this routing
    const MidiMessage& getLastMessage() const { return _lastMessage; }
    void setLastMessage(const MidiMessage& msg) {
        _lastMessage = msg;
        _messageCount++;
    }

    // Serialization for persistence
    String serialize() const;
    static MidiRouting deserialize(const String& data);

private:
    uint8_t _id;
    String _srcDeviceId;    // Source device unique ID
    String _dstDeviceId;    // Destination device unique ID
    ChannelFilter _channelFilter;
    bool _enabled;
    uint32_t _messageCount;
    MidiMessage _lastMessage;
};

// Routing Manager - handles all routing logic
class RoutingManager {
public:
    RoutingManager();
    ~RoutingManager();

    // Initialize
    void begin();

    // Routing management
    MidiRouting* addRouting(const String& srcDeviceId, const String& dstDeviceId);
    bool removeRouting(uint8_t routingId);
    MidiRouting* getRouting(uint8_t id);
    MidiRouting* getRoutingByIndex(size_t index);
    size_t getRoutingCount() const { return _routings.size(); }
    const std::vector<MidiRouting>& getRoutings() const { return _routings; }
    std::vector<MidiRouting>& getRoutings() { return _routings; }

    // Process incoming MIDI and route to appropriate outputs
    void routeMidiMessage(uint8_t srcDeviceId, const MidiMessage& msg);

    // Check if a specific routing is active
    bool isRoutingActive(uint8_t routingId) const;

    // Clear all routings
    void clearAll();

    // Save/Load routings
    bool saveRoutings();
    bool loadRoutings();

    // Statistics
    uint32_t getTotalMessagesRouted() const { return _totalMessagesRouted; }

private:
    std::vector<MidiRouting> _routings;
    uint8_t _nextRoutingId;
    uint32_t _totalMessagesRouted;

    // Find routings from a specific source device
    std::vector<MidiRouting*> findRoutingsFromDevice(const String& deviceUniqueId);
};

// Global routing manager instance
extern RoutingManager routingManager;

#endif // ROUTING_H
