/**
 * MIDI Router for M5Stack Cardputer
 *
 * A USB MIDI host/hub that routes MIDI messages between connected devices.
 *
 * Features:
 * - USB Host support for multiple MIDI devices via USB hub
 * - Configurable routings with channel filtering
 * - Persistent storage of routing configurations
 * - Visual feedback for MIDI activity
 * - Keyboard-based navigation and editing
 *
 * Controls:
 * - W/S: Navigate up/down
 * - A/D: Navigate left/right (in channel select)
 * - Enter: Select/Edit
 * - N: New routing
 * - D: Delete routing
 * - E: Edit routing
 * - Space: Toggle (in channel select)
 * - Q/Backspace: Back/Cancel
 */

#include <M5Cardputer.h>
#include "MidiDevice.h"
#include "Routing.h"
#include "Storage.h"
#include "UI.h"

// Application state
static bool systemInitialized = false;
static uint32_t lastStatusUpdate = 0;
constexpr uint32_t STATUS_UPDATE_INTERVAL = 1000;  // 1 second

// Forward declarations
void onMidiReceived(uint8_t deviceId, const MidiMessage& msg);
void onDeviceChange();
void printStartupInfo();

void setup() {
    // Initialize serial for debugging
    Serial.begin(115200);
    delay(100);

    Serial.println("\n=================================");
    Serial.println("  MIDI Router for M5 Cardputer");
    Serial.println("=================================\n");

    // Initialize M5Cardputer
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    // Initialize storage first
    Serial.println("Initializing storage...");
    if (!storageManager.begin()) {
        Serial.println("ERROR: Storage initialization failed!");
    }

    // Initialize USB Host for MIDI devices
    Serial.println("Initializing USB Host...");
    if (!deviceManager.begin()) {
        Serial.println("ERROR: USB Host initialization failed!");
        // Continue anyway - we can still configure routings
    }

    // Set up callbacks
    deviceManager.setReceiveCallback(onMidiReceived);
    deviceManager.setDeviceChangeCallback(onDeviceChange);

    // Initialize routing manager (loads saved routings)
    Serial.println("Loading routings...");
    routingManager.begin();

    // Initialize UI
    Serial.println("Initializing UI...");
    uiManager.begin();

    // Load known device names
    std::vector<std::pair<String, String>> knownDevices;
    storageManager.loadAllDeviceNames(knownDevices);
    for (const auto& kd : knownDevices) {
        deviceManager.addKnownDevice(kd.first, kd.second);
    }

    systemInitialized = true;
    printStartupInfo();

    Serial.println("\nSystem ready!");
    Serial.println("Use keyboard to navigate:");
    Serial.println("  W/S - Up/Down");
    Serial.println("  N - New routing");
    Serial.println("  E - Edit routing");
    Serial.println("  D - Delete routing");
    Serial.println();
}

void loop() {
    if (!systemInitialized) return;

    // Update USB Host (handles device enumeration and MIDI input)
    deviceManager.update();

    // Update UI (handles display and keyboard input)
    uiManager.update();

    // Periodic status update (for debugging)
    if (millis() - lastStatusUpdate > STATUS_UPDATE_INTERVAL) {
        lastStatusUpdate = millis();

        // Check for new devices and log status
        static size_t lastDeviceCount = 0;
        size_t currentDeviceCount = deviceManager.getDeviceCount();

        if (currentDeviceCount != lastDeviceCount) {
            Serial.printf("Device count changed: %d -> %d\n",
                         lastDeviceCount, currentDeviceCount);
            lastDeviceCount = currentDeviceCount;

            // List all devices
            for (size_t i = 0; i < currentDeviceCount; i++) {
                MidiDevice* dev = deviceManager.getDeviceByIndex(i);
                if (dev) {
                    Serial.printf("  [%d] %s (%s)\n",
                                 dev->getId(),
                                 dev->getName().c_str(),
                                 dev->isConnected() ? "connected" : "disconnected");
                }
            }
        }
    }

    // Small delay to prevent watchdog issues
    delay(1);
}

/**
 * Callback when MIDI message is received from a device
 */
void onMidiReceived(uint8_t deviceId, const MidiMessage& msg) {
    // Update device's last message
    MidiDevice* device = deviceManager.getDevice(deviceId);
    if (device) {
        device->setLastMessage(msg);
    }

    // Route the message through all applicable routings
    routingManager.routeMidiMessage(deviceId, msg);

    // Flash UI for any routing that used this message
    auto& routings = routingManager.getRoutings();
    for (const auto& routing : routings) {
        if (routing.getLastMessage().timestamp == msg.timestamp) {
            uiManager.flashMidiActivity(routing.getId());
        }
    }

    // Debug output
    Serial.printf("MIDI [%d]: %s Ch%d\n",
                 deviceId,
                 msg.getDescription().c_str(),
                 msg.channel);
}

/**
 * Callback when device list changes (connect/disconnect)
 */
void onDeviceChange() {
    Serial.println("Device list changed");

    // Save any new device names
    for (size_t i = 0; i < deviceManager.getDeviceCount(); i++) {
        MidiDevice* dev = deviceManager.getDeviceByIndex(i);
        if (dev) {
            storageManager.saveDeviceName(dev->getUniqueId(), dev->getName());
        }
    }

    // Refresh UI
    uiManager.invalidate();
}

/**
 * Print startup information
 */
void printStartupInfo() {
    Serial.println("\n--- Startup Info ---");
    Serial.printf("Routings loaded: %d\n", routingManager.getRoutingCount());
    Serial.printf("Known devices: %d\n", deviceManager.getDeviceCount());

    // List routings
    auto& routings = routingManager.getRoutings();
    for (const auto& routing : routings) {
        Serial.printf("  Routing %d: %s -> %s [%s] %s\n",
                     routing.getId(),
                     routing.getSourceDeviceId().c_str(),
                     routing.getDestDeviceId().c_str(),
                     routing.getChannelFilter().toString().c_str(),
                     routing.isEnabled() ? "enabled" : "disabled");
    }
    Serial.println("-------------------\n");
}
