#ifndef MIDI_TYPES_H
#define MIDI_TYPES_H

#include <Arduino.h>
#include <vector>
#include <string>

// Maximum number of MIDI devices supported
constexpr size_t MAX_MIDI_DEVICES = 8;
// Maximum number of routings
constexpr size_t MAX_ROUTINGS = 32;
// Maximum device name length
constexpr size_t MAX_DEVICE_NAME_LEN = 32;

// MIDI Channel definitions
constexpr uint8_t MIDI_CHANNEL_ALL = 0xFF;  // Route all channels
constexpr uint8_t MIDI_CHANNEL_MIN = 1;
constexpr uint8_t MIDI_CHANNEL_MAX = 16;

// MIDI Message types
enum class MidiMessageType : uint8_t {
    NOTE_OFF         = 0x80,
    NOTE_ON          = 0x90,
    POLY_AFTERTOUCH  = 0xA0,
    CONTROL_CHANGE   = 0xB0,
    PROGRAM_CHANGE   = 0xC0,
    AFTERTOUCH       = 0xD0,
    PITCH_BEND       = 0xE0,
    SYSTEM           = 0xF0
};

// MIDI message structure
struct MidiMessage {
    uint8_t status;      // Status byte (includes channel for channel messages)
    uint8_t data1;       // First data byte
    uint8_t data2;       // Second data byte (if applicable)
    uint8_t channel;     // Extracted channel (1-16)
    uint32_t timestamp;  // When message was received (millis)

    MidiMessage() : status(0), data1(0), data2(0), channel(0), timestamp(0) {}

    MidiMessage(uint8_t s, uint8_t d1, uint8_t d2 = 0)
        : status(s), data1(d1), data2(d2), timestamp(millis()) {
        channel = (s & 0x0F) + 1;  // Extract and convert to 1-16
    }

    // Get message type (status without channel)
    MidiMessageType getType() const {
        if ((status & 0xF0) == 0xF0) {
            return MidiMessageType::SYSTEM;
        }
        return static_cast<MidiMessageType>(status & 0xF0);
    }

    // Get a human-readable description
    String getDescription() const {
        String desc;
        switch (getType()) {
            case MidiMessageType::NOTE_ON:
                desc = "Note ON  ";
                desc += String(data1);
                desc += " v";
                desc += String(data2);
                break;
            case MidiMessageType::NOTE_OFF:
                desc = "Note OFF ";
                desc += String(data1);
                break;
            case MidiMessageType::CONTROL_CHANGE:
                desc = "CC ";
                desc += String(data1);
                desc += "=";
                desc += String(data2);
                break;
            case MidiMessageType::PROGRAM_CHANGE:
                desc = "PrgChg ";
                desc += String(data1);
                break;
            case MidiMessageType::PITCH_BEND:
                desc = "PitchB ";
                desc += String((data2 << 7) | data1);
                break;
            case MidiMessageType::AFTERTOUCH:
                desc = "AfterT ";
                desc += String(data1);
                break;
            case MidiMessageType::POLY_AFTERTOUCH:
                desc = "PolyAT ";
                desc += String(data1);
                break;
            case MidiMessageType::SYSTEM:
                desc = "System";
                break;
            default:
                desc = "Unknown";
        }
        return desc;
    }

    // Check if this is a channel message
    bool isChannelMessage() const {
        return (status & 0xF0) != 0xF0;
    }

    // Get message length (number of bytes including status)
    uint8_t getLength() const {
        switch (getType()) {
            case MidiMessageType::PROGRAM_CHANGE:
            case MidiMessageType::AFTERTOUCH:
                return 2;
            default:
                return 3;
        }
    }
};

// Device connection state
enum class DeviceState {
    DISCONNECTED,
    CONNECTED,
    ERROR
};

// Routing filter options
struct ChannelFilter {
    uint16_t channelMask;  // Bit mask for channels 1-16 (bit 0 = ch1, etc.)

    ChannelFilter() : channelMask(0xFFFF) {}  // All channels by default

    void setAll() { channelMask = 0xFFFF; }
    void setNone() { channelMask = 0; }
    void setChannel(uint8_t ch, bool enabled) {
        if (ch >= 1 && ch <= 16) {
            if (enabled) {
                channelMask |= (1 << (ch - 1));
            } else {
                channelMask &= ~(1 << (ch - 1));
            }
        }
    }
    bool isChannelEnabled(uint8_t ch) const {
        if (ch >= 1 && ch <= 16) {
            return (channelMask & (1 << (ch - 1))) != 0;
        }
        return false;
    }
    bool isAllChannels() const { return channelMask == 0xFFFF; }
    bool isNoChannels() const { return channelMask == 0; }

    // Count enabled channels
    uint8_t countEnabled() const {
        uint8_t count = 0;
        for (int i = 0; i < 16; i++) {
            if (channelMask & (1 << i)) count++;
        }
        return count;
    }

    // Get string representation
    String toString() const {
        if (isAllChannels()) return "All";
        if (isNoChannels()) return "None";
        if (countEnabled() == 1) {
            for (int i = 0; i < 16; i++) {
                if (channelMask & (1 << i)) {
                    return String(i + 1);
                }
            }
        }
        return String(countEnabled()) + "ch";
    }
};

#endif // MIDI_TYPES_H
