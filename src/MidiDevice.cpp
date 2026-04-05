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

// MIDI USB class codes (prefixed to avoid conflict with SDK macros)
static constexpr uint8_t MIDI_USB_CLASS_AUDIO = 0x01;
static constexpr uint8_t USB_SUBCLASS_MIDISTREAMING = 0x03;

// USB Host task handles
static TaskHandle_t s_usbHostTaskHandle = nullptr;
static TaskHandle_t s_usbClientTaskHandle = nullptr;

// Open MIDI device tracking
struct OpenMidiDev {
    bool active;
    uint8_t usbAddress;
    usb_device_handle_t devHandle;
    uint8_t interfaceNum;
    uint8_t bulkInEp;
    uint8_t bulkOutEp;
    uint16_t bulkInMps;
    uint16_t bulkOutMps;
    usb_transfer_t* inTransfer;
};
static constexpr int MAX_OPEN_MIDI = 4;
static OpenMidiDev s_openMidi[MAX_OPEN_MIDI];

// Forward declarations
static void usb_host_lib_task(void* arg);
static void usb_host_client_task(void* arg);
static void usb_host_client_event_callback(const usb_host_client_event_msg_t* event_msg, void* arg);
static void midi_bulk_in_cb(usb_transfer_t* transfer);
static OpenMidiDev* find_open_midi(uint8_t address);
static void teardown_midi_device(OpenMidiDev* dev);

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
        if (s_usbClientTaskHandle) {
            vTaskDelete(s_usbClientTaskHandle);
            s_usbClientTaskHandle = nullptr;
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
        .max_num_event_msg = 32,
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

    // Create dedicated task for client event handling (don't rely on main loop polling)
    xTaskCreate(usb_host_client_task, "usb_client", 4096, nullptr, 5, &s_usbClientTaskHandle);

    _initialized = true;
    ESP_LOGI(TAG, "USB Host MIDI initialized");
    return true;
}

void MidiDeviceManager::update() {
    if (!_initialized) return;

    // Client events are handled in usb_host_client_task (dedicated task)
    // Process incoming MIDI data
    processMidiInput();
}

void MidiDeviceManager::appendUsbLog(const String& entry) {
    if (_usbLog.size() >= USB_LOG_MAX) {
        _usbLog.erase(_usbLog.begin());
    }
    _usbLog.push_back(entry);
    ESP_LOGI(TAG, "USB: %s", entry.c_str());
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
    if (!device || !device->isConnected()) return false;

    return sendMidiRaw(device->getUsbAddress(), status, data1, data2);
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
    // MIDI data is received via USB bulk IN transfers (midi_bulk_in_cb)
    // and dispatched via dispatchMidiReceived — nothing to do here
}

void MidiDeviceManager::dispatchMidiReceived(uint8_t usbAddress, uint8_t status, uint8_t data1, uint8_t data2) {
    // Find the logical device for this USB address
    uint8_t deviceId = 0;
    for (auto& d : _devices) {
        if (d.getUsbAddress() == usbAddress) {
            deviceId = d.getId();
            break;
        }
    }
    if (deviceId == 0) return;

    MidiMessage msg;
    msg.status    = status;
    msg.data1     = data1;
    msg.data2     = data2;
    msg.channel   = (status & 0x0F) + 1;
    msg.timestamp = millis();

    MidiDevice* dev = getDevice(deviceId);
    if (dev) dev->setLastMessage(msg);

    if (_receiveCallback) _receiveCallback(deviceId, msg);
}

bool MidiDeviceManager::sendMidiRaw(uint8_t usbAddress, uint8_t status, uint8_t data1, uint8_t data2) {
    OpenMidiDev* slot = find_open_midi(usbAddress);
    if (!slot || !slot->active || slot->bulkOutEp == 0) return false;

    usb_transfer_t* transfer = nullptr;
    if (usb_host_transfer_alloc(4, 0, &transfer) != ESP_OK) return false;

    uint8_t cin = (status >> 4) & 0x0F;
    transfer->device_handle    = slot->devHandle;
    transfer->bEndpointAddress = slot->bulkOutEp;
    transfer->callback         = [](usb_transfer_t* t) { usb_host_transfer_free(t); };
    transfer->context          = nullptr;
    transfer->num_bytes        = 4;
    transfer->timeout_ms       = 100;
    transfer->data_buffer[0]   = cin;
    transfer->data_buffer[1]   = status;
    transfer->data_buffer[2]   = data1;
    transfer->data_buffer[3]   = data2;

    esp_err_t err = usb_host_transfer_submit(transfer);
    if (err != ESP_OK) {
        usb_host_transfer_free(transfer);
        return false;
    }
    return true;
}

//=============================================================================
// USB Host Tasks and Callbacks
//=============================================================================

static OpenMidiDev* find_open_midi(uint8_t address) {
    for (int i = 0; i < MAX_OPEN_MIDI; i++) {
        if (s_openMidi[i].active && s_openMidi[i].usbAddress == address) {
            return &s_openMidi[i];
        }
    }
    return nullptr;
}

static void teardown_midi_device(OpenMidiDev* dev) {
    if (!dev || !dev->active) return;
    dev->active = false;

    if (dev->inTransfer) {
        usb_host_transfer_free(dev->inTransfer);
        dev->inTransfer = nullptr;
    }
    if (dev->devHandle) {
        usb_host_interface_release(s_clientHandle, dev->devHandle, dev->interfaceNum);
        usb_host_device_close(s_clientHandle, dev->devHandle);
        dev->devHandle = nullptr;
    }
}

// Called when a bulk IN transfer completes (MIDI data received from device)
static void midi_bulk_in_cb(usb_transfer_t* transfer) {
    OpenMidiDev* dev = static_cast<OpenMidiDev*>(transfer->context);
    if (!dev || !dev->active || !s_managerInstance) return;

    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        int len = transfer->actual_num_bytes;
        // USB MIDI packets are 4 bytes each
        for (int i = 0; i + 3 < len; i += 4) {
            uint8_t cin  = transfer->data_buffer[i] & 0x0F;
            uint8_t b1   = transfer->data_buffer[i + 1];
            uint8_t b2   = transfer->data_buffer[i + 2];
            uint8_t b3   = transfer->data_buffer[i + 3];
            // CIN=0 or 0xF means padding/no-op
            if (cin == 0 || b1 == 0) continue;
            s_managerInstance->dispatchMidiReceived(dev->usbAddress, b1, b2, b3);
        }
    }

    // Re-submit for continuous reception
    if (dev->active) {
        transfer->num_bytes = dev->bulkInMps;
        usb_host_transfer_submit(transfer);
    }
}

static void usb_host_lib_task(void* arg) {
    while (true) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);

        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "No USB clients");
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "All USB devices freed");
        }
    }
}

static void usb_host_client_task(void* arg) {
    while (true) {
        if (s_clientHandle) {
            usb_host_client_handle_events(s_clientHandle, portMAX_DELAY);
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
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

            usb_device_handle_t dev_handle;
            esp_err_t err = usb_host_device_open(s_clientHandle, address, &dev_handle);
            if (err != ESP_OK) {
                char logbuf[32];
                snprintf(logbuf, sizeof(logbuf), "open err a%d", address);
                manager->appendUsbLog(logbuf);
                break;
            }

            const usb_device_desc_t* dev_desc;
            err = usb_host_get_device_descriptor(dev_handle, &dev_desc);
            if (err != ESP_OK) {
                char logbuf[40];
                snprintf(logbuf, sizeof(logbuf), "devdesc err a%d", address);
                manager->appendUsbLog(logbuf);
                usb_host_device_close(s_clientHandle, dev_handle);
                break;
            }

            // Get product name from string descriptor
            usb_device_info_t dev_info;
            String deviceName = "Unknown";
            if (usb_host_device_info(dev_handle, &dev_info) == ESP_OK &&
                dev_info.str_desc_product != nullptr) {
                const uint8_t* str = (const uint8_t*)dev_info.str_desc_product;
                uint8_t len = str[0];
                if (len > 2) {
                    char name[MAX_DEVICE_NAME_LEN];
                    int j = 0;
                    for (int i = 2; i < len && j < (int)MAX_DEVICE_NAME_LEN - 1; i += 2) {
                        if (str[i] >= 32 && str[i] < 127) name[j++] = str[i];
                    }
                    name[j] = '\0';
                    if (j > 0) deviceName = String(name);
                }
            }

            // Log device
            {
                char logbuf[48];
                snprintf(logbuf, sizeof(logbuf), "a%d cls%02X %s",
                         address, dev_desc->bDeviceClass, deviceName.substring(0, 18).c_str());
                manager->appendUsbLog(logbuf);
            }

            // Scan config descriptor for MIDI streaming interface + endpoints
            const usb_config_desc_t* config_desc;
            err = usb_host_get_active_config_descriptor(dev_handle, &config_desc);
            if (err != ESP_OK || config_desc == nullptr) {
                char logbuf[32];
                snprintf(logbuf, sizeof(logbuf), "cfgdesc err a%d", address);
                manager->appendUsbLog(logbuf);
                usb_host_device_close(s_clientHandle, dev_handle);
                break;
            }

            // Walk descriptors to find MIDI streaming interface and its bulk endpoints
            bool isMidi = false;
            uint8_t midiIntfNum = 0;
            uint8_t bulkIn = 0, bulkOut = 0;
            uint16_t bulkInMps = 64, bulkOutMps = 64;

            const uint8_t* p = (const uint8_t*)config_desc;
            int totalLen = config_desc->wTotalLength;
            int offset = 0;
            bool inMidiIntf = false;

            while (offset < totalLen) {
                if (offset + 2 > totalLen) break;
                uint8_t bLen  = p[offset];
                uint8_t bType = p[offset + 1];
                if (bLen == 0) break;

                if (bType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
                    const usb_intf_desc_t* intf = (const usb_intf_desc_t*)(p + offset);
                    inMidiIntf = (intf->bInterfaceClass == MIDI_USB_CLASS_AUDIO &&
                                  intf->bInterfaceSubClass == USB_SUBCLASS_MIDISTREAMING);
                    if (inMidiIntf) {
                        midiIntfNum = intf->bInterfaceNumber;
                        isMidi = true;
                    }
                } else if (bType == USB_B_DESCRIPTOR_TYPE_ENDPOINT && inMidiIntf) {
                    const usb_ep_desc_t* ep = (const usb_ep_desc_t*)(p + offset);
                    // Only care about bulk endpoints
                    if ((ep->bmAttributes & 0x03) == USB_BM_ATTRIBUTES_XFER_BULK) {
                        if (ep->bEndpointAddress & 0x80) {
                            bulkIn    = ep->bEndpointAddress;
                            bulkInMps = ep->wMaxPacketSize;
                        } else {
                            bulkOut    = ep->bEndpointAddress;
                            bulkOutMps = ep->wMaxPacketSize;
                        }
                    }
                }
                offset += bLen;
            }

            if (!isMidi || bulkIn == 0) {
                // Not MIDI or no usable IN endpoint — close and move on
                usb_host_device_close(s_clientHandle, dev_handle);
                break;
            }

            // Claim the MIDI streaming interface
            err = usb_host_interface_claim(s_clientHandle, dev_handle, midiIntfNum, 0);
            if (err != ESP_OK) {
                char logbuf[40];
                snprintf(logbuf, sizeof(logbuf), "claim err a%d: %s", address, esp_err_to_name(err));
                manager->appendUsbLog(logbuf);
                usb_host_device_close(s_clientHandle, dev_handle);
                break;
            }

            // Find a slot for this device
            OpenMidiDev* slot = nullptr;
            for (int i = 0; i < MAX_OPEN_MIDI; i++) {
                if (!s_openMidi[i].active) { slot = &s_openMidi[i]; break; }
            }
            if (!slot) {
                manager->appendUsbLog("no slots");
                usb_host_interface_release(s_clientHandle, dev_handle, midiIntfNum);
                usb_host_device_close(s_clientHandle, dev_handle);
                break;
            }

            slot->active       = true;
            slot->usbAddress   = address;
            slot->devHandle    = dev_handle;
            slot->interfaceNum = midiIntfNum;
            slot->bulkInEp     = bulkIn;
            slot->bulkOutEp    = bulkOut;
            slot->bulkInMps    = bulkInMps;
            slot->bulkOutMps   = bulkOutMps;
            slot->inTransfer   = nullptr;

            // Allocate and submit bulk IN transfer
            err = usb_host_transfer_alloc(bulkInMps, 0, &slot->inTransfer);
            if (err == ESP_OK) {
                slot->inTransfer->device_handle    = dev_handle;
                slot->inTransfer->bEndpointAddress = bulkIn;
                slot->inTransfer->callback         = midi_bulk_in_cb;
                slot->inTransfer->context          = slot;
                slot->inTransfer->num_bytes        = bulkInMps;
                slot->inTransfer->timeout_ms       = 0;  // no timeout for bulk IN
                usb_host_transfer_submit(slot->inTransfer);
            }

            manager->onDeviceConnected(address, dev_desc->idVendor,
                                       dev_desc->idProduct, deviceName);
            break;
        }

        case USB_HOST_CLIENT_EVENT_DEV_GONE: {
            usb_device_handle_t gone_hdl = event_msg->dev_gone.dev_hdl;
            ESP_LOGI(TAG, "Device disconnected");

            // Find open MIDI device by handle
            uint8_t address = 0;
            for (int i = 0; i < MAX_OPEN_MIDI; i++) {
                if (s_openMidi[i].active && s_openMidi[i].devHandle == gone_hdl) {
                    address = s_openMidi[i].usbAddress;
                    teardown_midi_device(&s_openMidi[i]);
                    break;
                }
            }

            manager->onDeviceDisconnected(address);
            break;
        }

        default:
            break;
    }
}
