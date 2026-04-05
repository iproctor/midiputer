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

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "Cardputer.h"
#include "MidiDevice.h"
#include "Routing.h"
#include "Storage.h"
#include "UI.h"

static const char* TAG = "Main";

// Application state
static bool systemInitialized = false;
static uint32_t lastStatusUpdate = 0;
constexpr uint32_t STATUS_UPDATE_INTERVAL = 1000;  // 1 second

// Forward declarations
void onMidiReceived(uint8_t deviceId, const MidiMessage& msg);
void onDeviceChange();
void printStartupInfo();

extern "C" void app_main(void) {
    // Initialize NVS flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "  MIDI Router for M5 Cardputer");
    ESP_LOGI(TAG, "=================================");

    // Initialize display and keyboard
    display.init();
    keyboard.begin();

    // Initialize storage first
    ESP_LOGI(TAG, "Initializing storage...");
    if (!storageManager.begin()) {
        ESP_LOGE(TAG, "Storage initialization failed!");
    }

    // Initialize USB Host for MIDI devices
    ESP_LOGI(TAG, "Initializing USB Host...");
    if (!deviceManager.begin()) {
        ESP_LOGE(TAG, "USB Host initialization failed!");
        // Continue anyway - we can still configure routings
    }

    // Set up callbacks
    deviceManager.setReceiveCallback(onMidiReceived);
    deviceManager.setDeviceChangeCallback(onDeviceChange);

    // Initialize routing manager (loads saved routings)
    ESP_LOGI(TAG, "Loading routings...");
    routingManager.begin();

    // Initialize UI
    ESP_LOGI(TAG, "Initializing UI...");
    uiManager.begin();

    // Load known device names
    std::vector<std::pair<String, String>> knownDevices;
    storageManager.loadAllDeviceNames(knownDevices);
    for (const auto& kd : knownDevices) {
        deviceManager.addKnownDevice(kd.first, kd.second);
    }

    systemInitialized = true;
    printStartupInfo();

    ESP_LOGI(TAG, "System ready!");
    ESP_LOGI(TAG, "Use keyboard to navigate: W/S=Up/Down N=New E=Edit D=Delete");

    while (true) {
        if (systemInitialized) {
            // Update USB Host (handles device enumeration and MIDI input)
            deviceManager.update();

            // Update UI (handles display and keyboard input)
            uiManager.update();

            // Periodic status update (for debugging)
            if (millis() - lastStatusUpdate > STATUS_UPDATE_INTERVAL) {
                lastStatusUpdate = millis();

                static size_t lastDeviceCount = 0;
                size_t currentDeviceCount = deviceManager.getDeviceCount();

                if (currentDeviceCount != lastDeviceCount) {
                    ESP_LOGI(TAG, "Device count changed: %d -> %d",
                             (int)lastDeviceCount, (int)currentDeviceCount);
                    lastDeviceCount = currentDeviceCount;

                    for (size_t i = 0; i < currentDeviceCount; i++) {
                        MidiDevice* dev = deviceManager.getDeviceByIndex(i);
                        if (dev) {
                            ESP_LOGI(TAG, "  [%d] %s (%s)",
                                     dev->getId(),
                                     dev->getName().c_str(),
                                     dev->isConnected() ? "connected" : "disconnected");
                        }
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
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

    ESP_LOGI(TAG, "MIDI [%d]: %s Ch%d",
             deviceId,
             msg.getDescription().c_str(),
             msg.channel);
}

/**
 * Callback when device list changes (connect/disconnect)
 */
void onDeviceChange() {
    ESP_LOGI(TAG, "Device list changed");

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
    ESP_LOGI(TAG, "--- Startup Info ---");
    ESP_LOGI(TAG, "Routings loaded: %d", (int)routingManager.getRoutingCount());
    ESP_LOGI(TAG, "Known devices: %d", (int)deviceManager.getDeviceCount());

    auto& routings = routingManager.getRoutings();
    for (const auto& routing : routings) {
        ESP_LOGI(TAG, "  Routing %d: %s -> %s [%s] %s",
                 routing.getId(),
                 routing.getSourceDeviceId().c_str(),
                 routing.getDestDeviceId().c_str(),
                 routing.getChannelFilter().toString().c_str(),
                 routing.isEnabled() ? "enabled" : "disabled");
    }
    ESP_LOGI(TAG, "-------------------");
}
