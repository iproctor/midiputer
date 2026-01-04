#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <Preferences.h>
#include <vector>
#include "Routing.h"

// Storage manager for persistent configuration
class StorageManager {
public:
    StorageManager();
    ~StorageManager();

    // Initialize storage
    bool begin();
    void end();

    // Routing persistence
    bool saveRoutings(const std::vector<MidiRouting>& routings);
    bool loadRoutings(std::vector<MidiRouting>& routings);
    bool clearRoutings();

    // Device name persistence (for remembering device names by unique ID)
    bool saveDeviceName(const String& uniqueId, const String& name);
    String loadDeviceName(const String& uniqueId);
    bool loadAllDeviceNames(std::vector<std::pair<String, String>>& devices);

    // Settings
    bool saveSettings(uint8_t brightness, bool soundEnabled);
    bool loadSettings(uint8_t& brightness, bool& soundEnabled);

    // Get storage statistics
    size_t getFreeSpace();
    size_t getUsedSpace();

private:
    Preferences _prefs;
    bool _initialized;

    // Namespace names
    static constexpr const char* NS_ROUTINGS = "routings";
    static constexpr const char* NS_DEVICES = "devices";
    static constexpr const char* NS_SETTINGS = "settings";

    // Keys
    static constexpr const char* KEY_ROUTING_COUNT = "count";
    static constexpr const char* KEY_ROUTING_PREFIX = "r";
    static constexpr const char* KEY_DEVICE_COUNT = "count";
    static constexpr const char* KEY_DEVICE_PREFIX = "d";
    static constexpr const char* KEY_BRIGHTNESS = "bright";
    static constexpr const char* KEY_SOUND = "sound";
};

// Global storage manager instance
extern StorageManager storageManager;

#endif // STORAGE_H
