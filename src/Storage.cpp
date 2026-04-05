#include "Storage.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "Storage";

// Global instance
StorageManager storageManager;

//=============================================================================
// Helper: get a string from NVS (returns empty String on failure)
//=============================================================================
static String nvs_get_string_helper(nvs_handle_t h, const char* key) {
    size_t len = 0;
    esp_err_t err = nvs_get_str(h, key, NULL, &len);
    if (err != ESP_OK || len == 0) return String();

    char* buf = (char*)malloc(len);
    if (!buf) return String();

    err = nvs_get_str(h, key, buf, &len);
    String result;
    if (err == ESP_OK) {
        result = String(buf);
    }
    free(buf);
    return result;
}

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
    _initialized = false;
}

bool StorageManager::saveRoutings(const std::vector<MidiRouting>& routings) {
    if (!_initialized) return false;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_ROUTINGS, NVS_READWRITE, &h);
    if (err != ESP_OK) return false;

    nvs_set_u32(h, KEY_ROUTING_COUNT, (uint32_t)routings.size());

    for (size_t i = 0; i < routings.size(); i++) {
        char key[16];
        snprintf(key, sizeof(key), "%s%zu", KEY_ROUTING_PREFIX, i);
        String data = routings[i].serialize();
        nvs_set_str(h, key, data.c_str());
    }

    // Clear old entries beyond current count
    for (size_t i = routings.size(); i < MAX_ROUTINGS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "%s%zu", KEY_ROUTING_PREFIX, i);
        esp_err_t e = nvs_erase_key(h, key);
        if (e == ESP_ERR_NVS_NOT_FOUND) break;
    }

    nvs_commit(h);
    nvs_close(h);

    ESP_LOGI(TAG, "Saved %d routings", (int)routings.size());
    return true;
}

bool StorageManager::loadRoutings(std::vector<MidiRouting>& routings) {
    if (!_initialized) return false;

    routings.clear();

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_ROUTINGS, NVS_READONLY, &h);
    if (err != ESP_OK) return false;

    uint32_t count = 0;
    nvs_get_u32(h, KEY_ROUTING_COUNT, &count);

    for (size_t i = 0; i < count && i < MAX_ROUTINGS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "%s%zu", KEY_ROUTING_PREFIX, i);
        String data = nvs_get_string_helper(h, key);

        if (data.length() > 0) {
            MidiRouting routing = MidiRouting::deserialize(data);
            if (routing.getId() > 0) {
                routings.push_back(routing);
            }
        }
    }

    nvs_close(h);

    ESP_LOGI(TAG, "Loaded %d routings", (int)routings.size());
    return true;
}

bool StorageManager::clearRoutings() {
    if (!_initialized) return false;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_ROUTINGS, NVS_READWRITE, &h);
    if (err != ESP_OK) return false;

    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGI(TAG, "Cleared all routings");
    return true;
}

bool StorageManager::saveDeviceName(const String& uniqueId, const String& name) {
    if (!_initialized) return false;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_DEVICES, NVS_READWRITE, &h);
    if (err != ESP_OK) return false;

    uint32_t count = 0;
    nvs_get_u32(h, KEY_DEVICE_COUNT, &count);

    bool found = false;
    for (size_t i = 0; i < count; i++) {
        char idKey[16];
        snprintf(idKey, sizeof(idKey), "%s%zu_id", KEY_DEVICE_PREFIX, i);
        String existingId = nvs_get_string_helper(h, idKey);
        if (existingId == uniqueId) {
            char nameKey[16];
            snprintf(nameKey, sizeof(nameKey), "%s%zu_nm", KEY_DEVICE_PREFIX, i);
            nvs_set_str(h, nameKey, name.c_str());
            found = true;
            break;
        }
    }

    if (!found) {
        char idKey[16];
        char nameKey[16];
        snprintf(idKey,   sizeof(idKey),   "%s%u_id", KEY_DEVICE_PREFIX, (unsigned)count);
        snprintf(nameKey, sizeof(nameKey), "%s%u_nm", KEY_DEVICE_PREFIX, (unsigned)count);
        nvs_set_str(h, idKey,   uniqueId.c_str());
        nvs_set_str(h, nameKey, name.c_str());
        nvs_set_u32(h, KEY_DEVICE_COUNT, count + 1);
    }

    nvs_commit(h);
    nvs_close(h);
    return true;
}

String StorageManager::loadDeviceName(const String& uniqueId) {
    if (!_initialized) return String();

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_DEVICES, NVS_READONLY, &h);
    if (err != ESP_OK) return String();

    uint32_t count = 0;
    nvs_get_u32(h, KEY_DEVICE_COUNT, &count);

    String result;
    for (size_t i = 0; i < count; i++) {
        char idKey[16];
        snprintf(idKey, sizeof(idKey), "%s%zu_id", KEY_DEVICE_PREFIX, i);
        String existingId = nvs_get_string_helper(h, idKey);
        if (existingId == uniqueId) {
            char nameKey[16];
            snprintf(nameKey, sizeof(nameKey), "%s%zu_nm", KEY_DEVICE_PREFIX, i);
            result = nvs_get_string_helper(h, nameKey);
            break;
        }
    }

    nvs_close(h);
    return result;
}

bool StorageManager::loadAllDeviceNames(std::vector<std::pair<String, String>>& devices) {
    if (!_initialized) return false;

    devices.clear();

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_DEVICES, NVS_READONLY, &h);
    if (err != ESP_OK) return false;

    uint32_t count = 0;
    nvs_get_u32(h, KEY_DEVICE_COUNT, &count);

    for (size_t i = 0; i < count; i++) {
        char idKey[16];
        char nameKey[16];
        snprintf(idKey,   sizeof(idKey),   "%s%zu_id", KEY_DEVICE_PREFIX, i);
        snprintf(nameKey, sizeof(nameKey), "%s%zu_nm", KEY_DEVICE_PREFIX, i);

        String id   = nvs_get_string_helper(h, idKey);
        String name = nvs_get_string_helper(h, nameKey);

        if (id.length() > 0) {
            devices.push_back({id, name});
        }
    }

    nvs_close(h);
    return true;
}

bool StorageManager::saveSettings(uint8_t brightness, bool soundEnabled) {
    if (!_initialized) return false;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_SETTINGS, NVS_READWRITE, &h);
    if (err != ESP_OK) return false;

    nvs_set_u32(h, KEY_BRIGHTNESS, brightness);
    nvs_set_u32(h, KEY_SOUND, soundEnabled ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
    return true;
}

bool StorageManager::loadSettings(uint8_t& brightness, bool& soundEnabled) {
    if (!_initialized) return false;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_SETTINGS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        brightness    = 128;
        soundEnabled  = true;
        return false;
    }

    uint32_t bright = 128;
    uint32_t sound  = 1;
    nvs_get_u32(h, KEY_BRIGHTNESS, &bright);
    nvs_get_u32(h, KEY_SOUND,      &sound);
    nvs_close(h);

    brightness   = (uint8_t)bright;
    soundEnabled = (sound != 0);
    return true;
}

size_t StorageManager::getFreeSpace() {
    return 0;
}

size_t StorageManager::getUsedSpace() {
    return 0;
}
