#ifndef MIDI_DEVICE_H
#define MIDI_DEVICE_H

#include "MidiTypes.h"
#include <functional>

// Callback type for received MIDI messages
using MidiReceiveCallback = std::function<void(uint8_t deviceId, const MidiMessage& msg)>;

// MIDI Device information and state
class MidiDevice {
public:
    MidiDevice();
    MidiDevice(uint8_t id, const String& name, uint16_t vid = 0, uint16_t pid = 0);

    // Identification
    uint8_t getId() const { return _id; }
    String getName() const { return _name; }
    uint16_t getVID() const { return _vid; }
    uint16_t getPID() const { return _pid; }

    // State
    DeviceState getState() const { return _state; }
    bool isConnected() const { return _state == DeviceState::CONNECTED; }
    void setState(DeviceState state) { _state = state; }

    // USB Address (for matching physical devices)
    uint8_t getUsbAddress() const { return _usbAddress; }
    void setUsbAddress(uint8_t addr) { _usbAddress = addr; }

    // Generate a unique identifier string for persistence
    String getUniqueId() const;

    // Statistics
    uint32_t getMessageCount() const { return _messageCount; }
    void incrementMessageCount() { _messageCount++; }
    void resetMessageCount() { _messageCount = 0; }

    // Last received message
    const MidiMessage& getLastMessage() const { return _lastMessage; }
    void setLastMessage(const MidiMessage& msg) {
        _lastMessage = msg;
        _messageCount++;
    }

    // Comparison for matching devices
    bool matches(uint16_t vid, uint16_t pid, const String& name) const;
    bool matches(const String& uniqueId) const;

    // Serialization
    String serialize() const;
    static MidiDevice deserialize(const String& data);

private:
    uint8_t _id;
    String _name;
    uint16_t _vid;  // USB Vendor ID
    uint16_t _pid;  // USB Product ID
    DeviceState _state;
    uint8_t _usbAddress;
    uint32_t _messageCount;
    MidiMessage _lastMessage;
};

// MIDI Device Manager - handles USB host enumeration
class MidiDeviceManager {
public:
    MidiDeviceManager();
    ~MidiDeviceManager();

    // Initialize USB Host
    bool begin();

    // Poll for USB events (call frequently)
    void update();

    // Get devices
    size_t getDeviceCount() const { return _devices.size(); }
    MidiDevice* getDevice(uint8_t id);
    MidiDevice* getDeviceByIndex(size_t index);
    const std::vector<MidiDevice>& getDevices() const { return _devices; }

    // Find device by unique ID
    MidiDevice* findDevice(const String& uniqueId);

    // Send MIDI message to a device
    bool sendMidi(uint8_t deviceId, const MidiMessage& msg);
    bool sendMidi(uint8_t deviceId, uint8_t status, uint8_t data1, uint8_t data2 = 0);

    // Set callback for received MIDI
    void setReceiveCallback(MidiReceiveCallback callback) { _receiveCallback = callback; }

    // Device list changed callback
    using DeviceChangeCallback = std::function<void()>;
    void setDeviceChangeCallback(DeviceChangeCallback callback) { _deviceChangeCallback = callback; }

    // Get known (persisted) device names
    void addKnownDevice(const String& uniqueId, const String& name);
    String getKnownDeviceName(const String& uniqueId) const;

private:
    std::vector<MidiDevice> _devices;
    std::vector<std::pair<String, String>> _knownDevices;  // uniqueId -> name
    MidiReceiveCallback _receiveCallback;
    DeviceChangeCallback _deviceChangeCallback;
    uint8_t _nextDeviceId;
    bool _initialized;

    // Internal USB handling
    void onDeviceConnected(uint8_t address, uint16_t vid, uint16_t pid, const String& name);
    void onDeviceDisconnected(uint8_t address);
    void processMidiInput();
};

// Global device manager instance
extern MidiDeviceManager deviceManager;

#endif // MIDI_DEVICE_H
