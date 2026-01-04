#include "MidiDevice.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// USB Host includes (ESP-IDF)
#include "usb/usb_host.h"

static const char* TAG = "MidiDevice";

// Global instance
MidiDeviceManager deviceManager;

// USB Host state
static usb_host_client_handle_t s_clientHandle = nullptr;
static bool s_usbHostInstalled = false;
static SemaphoreHandle_t s_deviceMutex = nullptr;
static MidiDeviceManager* s_managerInstance = nullptr;

// MIDI USB class codes
static constexpr uint8_t USB_CLASS_AUDIO = 0x01;
static constexpr uint8_t USB_SUBCLASS_MIDISTREAMING = 0x03;

// USB Host task handle
static TaskHandle_t s_usbHostTaskHandle = nullptr;

// Forward declarations
static void usb_host_lib_task(void* arg);
static void usb_host_client_event_callback(const usb_host_client_event_msg_t* event_msg, void* arg);

//=============================================================================
// MidiDevice Implementation
//=============================================================================

MidiDevice::MidiDevice()
    : _id(0), _name(""), _vid(0), _pid(0),
      _state(DeviceState::DISCONNECTED), _usbAddress(0),
      _messageCount(0) {
}

MidiDevice::MidiDevice(uint8_t id, const String& name, uint16_t vid, uint16_t pid)
    : _id(id), _name(name), _vid(vid), _pid(pid),
      _state(DeviceState::DISCONNECTED), _usbAddress(0),
      _messageCount(0) {
}

String MidiDevice::getUniqueId() const {
    char buf[64];
    snprintf(buf, sizeof(buf), "%04X:%04X:%s", _vid, _pid, _name.c_str());
    return String(buf);
}

bool MidiDevice::matches(uint16_t vid, uint16_t pid, const String& name) const {
    return _vid == vid && _pid == pid && _name == name;
}

bool MidiDevice::matches(const String& uniqueId) const {
    return getUniqueId() == uniqueId;
}

String MidiDevice::serialize() const {
    char buf[128];
    snprintf(buf, sizeof(buf), "%d|%04X|%04X|%s",
             _id, _vid, _pid, _name.c_str());
    return String(buf);
}

MidiDevice MidiDevice::deserialize(const String& data) {
    MidiDevice device;
    int id, vid, pid;
    char name[MAX_DEVICE_NAME_LEN];

    if (sscanf(data.c_str(), "%d|%04X|%04X|%31[^\n]",
               &id, &vid, &pid, name) == 4) {
        device._id = id;
        device._vid = vid;
        device._pid = pid;
        device._name = String(name);
    }
    return device;
}

//=============================================================================
// MidiDeviceManager Implementation
//=============================================================================

MidiDeviceManager::MidiDeviceManager()
    : _nextDeviceId(1), _initialized(false) {
}

MidiDeviceManager::~MidiDeviceManager() {
    if (_initialized) {
        if (s_usbHostTaskHandle) {
            vTaskDelete(s_usbHostTaskHandle);
            s_usbHostTaskHandle = nullptr;
        }
        if (s_clientHandle) {
            usb_host_client_deregister(s_clientHandle);
            s_clientHandle = nullptr;
        }
        if (s_usbHostInstalled) {
            usb_host_uninstall();
            s_usbHostInstalled = false;
        }
        if (s_deviceMutex) {
            vSemaphoreDelete(s_deviceMutex);
            s_deviceMutex = nullptr;
        }
    }
}

bool MidiDeviceManager::begin() {
    if (_initialized) return true;

    ESP_LOGI(TAG, "Initializing USB Host for MIDI");

    s_managerInstance = this;

    // Create mutex for thread safety
    s_deviceMutex = xSemaphoreCreateMutex();
    if (!s_deviceMutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }

    // Install USB Host Library
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install USB Host: %s", esp_err_to_name(err));
        // Continue without USB - allows testing UI without actual devices
        _initialized = true;
        return true;
    }
    s_usbHostInstalled = true;

    // Register client
    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = usb_host_client_event_callback,
            .callback_arg = this,
        },
    };

    err = usb_host_client_register(&client_config, &s_clientHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register USB client: %s", esp_err_to_name(err));
        return false;
    }

    // Create USB Host library handling task
    xTaskCreate(usb_host_lib_task, "usb_host", 4096, nullptr, 5, &s_usbHostTaskHandle);

    _initialized = true;
    ESP_LOGI(TAG, "USB Host MIDI initialized");
    return true;
}

void MidiDeviceManager::update() {
    if (!_initialized || !s_clientHandle) return;

    // Handle client events (non-blocking)
    usb_host_client_handle_events(s_clientHandle, 0);

    // Process incoming MIDI data
    processMidiInput();
}

MidiDevice* MidiDeviceManager::getDevice(uint8_t id) {
    for (auto& device : _devices) {
        if (device.getId() == id) {
            return &device;
        }
    }
    return nullptr;
}

MidiDevice* MidiDeviceManager::getDeviceByIndex(size_t index) {
    if (index < _devices.size()) {
        return &_devices[index];
    }
    return nullptr;
}

MidiDevice* MidiDeviceManager::findDevice(const String& uniqueId) {
    for (auto& device : _devices) {
        if (device.matches(uniqueId)) {
            return &device;
        }
    }
    return nullptr;
}

bool MidiDeviceManager::sendMidi(uint8_t deviceId, const MidiMessage& msg) {
    return sendMidi(deviceId, msg.status, msg.data1, msg.data2);
}

bool MidiDeviceManager::sendMidi(uint8_t deviceId, uint8_t status, uint8_t data1, uint8_t data2) {
    MidiDevice* device = getDevice(deviceId);
    if (!device || !device->isConnected()) {
        return false;
    }

    // Create USB MIDI packet (4 bytes)
    // Byte 0: Cable Number (high nibble) | Code Index Number (low nibble)
    // Bytes 1-3: MIDI data

    uint8_t cin = (status >> 4) & 0x0F;  // Code Index Number = status high nibble
    uint8_t packet[4] = {
        cin,          // Cable 0, CIN from status
        status,
        data1,
        data2
    };

    // TODO: Submit USB transfer to device OUT endpoint
    ESP_LOGD(TAG, "Send MIDI to device %d: %02X %02X %02X",
             deviceId, status, data1, data2);

    return true;
}

void MidiDeviceManager::addKnownDevice(const String& uniqueId, const String& name) {
    for (auto& kd : _knownDevices) {
        if (kd.first == uniqueId) {
            kd.second = name;
            return;
        }
    }
    _knownDevices.push_back({uniqueId, name});
}

String MidiDeviceManager::getKnownDeviceName(const String& uniqueId) const {
    for (const auto& kd : _knownDevices) {
        if (kd.first == uniqueId) {
            return kd.second;
        }
    }
    return "";
}

void MidiDeviceManager::onDeviceConnected(uint8_t address, uint16_t vid, uint16_t pid, const String& name) {
    if (!s_deviceMutex) return;
    if (xSemaphoreTake(s_deviceMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    // Check if this device already exists (reconnecting)
    for (auto& device : _devices) {
        if (device.matches(vid, pid, name)) {
            device.setState(DeviceState::CONNECTED);
            device.setUsbAddress(address);
            ESP_LOGI(TAG, "Device reconnected: %s", name.c_str());
            xSemaphoreGive(s_deviceMutex);
            if (_deviceChangeCallback) {
                _deviceChangeCallback();
            }
            return;
        }
    }

    // Add new device
    MidiDevice device(_nextDeviceId++, name, vid, pid);
    device.setState(DeviceState::CONNECTED);
    device.setUsbAddress(address);
    _devices.push_back(device);

    ESP_LOGI(TAG, "New MIDI device connected: %s (VID:%04X PID:%04X)",
             name.c_str(), vid, pid);

    xSemaphoreGive(s_deviceMutex);

    if (_deviceChangeCallback) {
        _deviceChangeCallback();
    }
}

void MidiDeviceManager::onDeviceDisconnected(uint8_t address) {
    if (!s_deviceMutex) return;
    if (xSemaphoreTake(s_deviceMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    for (auto& device : _devices) {
        if (device.getUsbAddress() == address || address == 0) {
            if (device.isConnected()) {
                device.setState(DeviceState::DISCONNECTED);
                device.setUsbAddress(0);
                ESP_LOGI(TAG, "Device disconnected: %s", device.getName().c_str());
                break;
            }
        }
    }

    xSemaphoreGive(s_deviceMutex);

    if (_deviceChangeCallback) {
        _deviceChangeCallback();
    }
}

void MidiDeviceManager::processMidiInput() {
    // This is called from the main loop to check for incoming MIDI data
    // The actual MIDI data would be received via USB bulk transfers
    // and parsed here into MidiMessage structures

    // TODO: Implement USB MIDI bulk IN transfer handling
    // For each connected device:
    // 1. Check if data is available on IN endpoint
    // 2. Read USB MIDI packets (4 bytes each)
    // 3. Parse into MidiMessage
    // 4. Call receive callback
}

//=============================================================================
// USB Host Tasks and Callbacks
//=============================================================================

static void usb_host_lib_task(void* arg) {
    while (true) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);

        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            // All clients deregistered
            ESP_LOGI(TAG, "No USB clients");
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            // All devices freed
            ESP_LOGI(TAG, "All USB devices freed");
        }
    }
}

static void usb_host_client_event_callback(const usb_host_client_event_msg_t* event_msg, void* arg) {
    MidiDeviceManager* manager = static_cast<MidiDeviceManager*>(arg);
    if (!manager) return;

    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV: {
            uint8_t address = event_msg->new_dev.address;
            ESP_LOGI(TAG, "New device connected, address: %d", address);

            // Open device
            usb_device_handle_t dev_handle;
            esp_err_t err = usb_host_device_open(s_clientHandle, address, &dev_handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to open device: %s", esp_err_to_name(err));
                break;
            }

            // Get device descriptor
            const usb_device_desc_t* dev_desc;
            err = usb_host_get_device_desc(dev_handle, &dev_desc);
            if (err != ESP_OK) {
                usb_host_device_close(s_clientHandle, dev_handle);
                break;
            }

            // Get device info for product name
            usb_device_info_t dev_info;
            err = usb_host_device_info(dev_handle, &dev_info);

            String deviceName = "MIDI Device";
            if (err == ESP_OK && dev_info.str_desc_product != nullptr) {
                // USB string descriptors are UTF-16LE with 2-byte header
                const uint8_t* str = (const uint8_t*)dev_info.str_desc_product;
                uint8_t len = str[0];  // Total length including header
                if (len > 2) {
                    char name[MAX_DEVICE_NAME_LEN];
                    int j = 0;
                    for (int i = 2; i < len && j < (int)MAX_DEVICE_NAME_LEN - 1; i += 2) {
                        if (str[i] >= 32 && str[i] < 127) {
                            name[j++] = str[i];
                        }
                    }
                    name[j] = '\0';
                    if (j > 0) {
                        deviceName = String(name);
                    }
                }
            }

            // Check configuration descriptor for MIDI interface
            const usb_config_desc_t* config_desc;
            err = usb_host_get_active_config_descriptor(dev_handle, &config_desc);

            bool isMidi = false;
            if (err == ESP_OK && config_desc != nullptr) {
                const uint8_t* p = (const uint8_t*)config_desc;
                int offset = 0;

                while (offset < config_desc->wTotalLength) {
                    const usb_standard_desc_t* desc = (const usb_standard_desc_t*)(p + offset);
                    if (desc->bLength == 0) break;

                    if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
                        const usb_intf_desc_t* intf = (const usb_intf_desc_t*)desc;
                        if (intf->bInterfaceClass == USB_CLASS_AUDIO &&
                            intf->bInterfaceSubClass == USB_SUBCLASS_MIDISTREAMING) {
                            isMidi = true;
                            ESP_LOGI(TAG, "Found MIDI interface");
                            break;
                        }
                    }
                    offset += desc->bLength;
                }
            }

            if (isMidi) {
                manager->onDeviceConnected(address, dev_desc->idVendor,
                                          dev_desc->idProduct, deviceName);
                // Note: Keep device open for MIDI communication
                // In full implementation, claim interface and set up transfers
            } else {
                ESP_LOGI(TAG, "Device is not MIDI: %s (class=%02X)",
                        deviceName.c_str(), dev_desc->bDeviceClass);
            }

            usb_host_device_close(s_clientHandle, dev_handle);
            break;
        }

        case USB_HOST_CLIENT_EVENT_DEV_GONE: {
            ESP_LOGI(TAG, "Device disconnected");
            // The address isn't provided in DEV_GONE, so we mark all as potentially disconnected
            // A more robust implementation would track device handles
            manager->onDeviceDisconnected(0);
            break;
        }

        default:
            break;
    }
}
