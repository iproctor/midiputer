# CLAUDE.md — Midiputer

## Build

```sh
pio run                  # build
pio run -t upload        # flash (Cardputer must be in bootloader mode first)
pio run -t monitor       # serial monitor (only works when NOT using USB for MIDI)
```

**Bootloader mode:** hold Boot, tap Reset, release Boot.

The build takes ~60s clean, ~15s incremental.

## Platform setup (important)

- `platformio.ini` uses `platform = espressif32` (latest, currently 6.13.0) with `framework = espidf`
- This requires ESP-IDF 5.5.3 which needs Python ≥ 3.11 (`cryptography >= 44.0.0` excludes Python 3.9.0)
- The PlatformIO virtualenv `~/.platformio/penv/.espidf-5.5.3/` was manually created with `/usr/local/bin/python3.11`
- `sdkconfig.m5stack-cardputer` is the live sdkconfig — edit it directly for Kconfig changes; `sdkconfig.defaults` is a backup/reference but PlatformIO does not reliably apply it on rebuilds
- Key sdkconfig settings:
  - `CONFIG_USB_HOST_HUBS_SUPPORTED=y` — enables external USB hub enumeration (not in Arduino-ESP32)
  - `CONFIG_USB_HOST_HUB_MULTI_LEVEL=y` — enables daisy-chained hubs
  - `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=1024` — needed for devices with large config descriptors

## Hardware

- M5Stack Cardputer (ESP32-S3, `board = m5stack-stamps3`)
- ST7789 display, 135×240, SPI3_HOST, rotation=1 (landscape)
- Keyboard: GPIO matrix (NOT TCA8418 — that's the ADV model)
- USB host on the built-in USB-C port
- Requires externally powered USB hub; the Cardputer USB port cannot power devices

## Key design decisions

**ESP-IDF not Arduino:** Switched from `framework = arduino` to `framework = espidf` specifically to get `CONFIG_USB_HOST_HUBS_SUPPORTED`. Arduino-ESP32's pre-compiled SDK omits this entirely.

**arduino_compat.h:** Provides `String` (inherits `std::string`), `millis()`, `delay()` shims. The String class has `operator const char*()` so it works with LovyanGFX's `print()`. Do not add `size_t` constructor or `operator+(size_t)` — on 32-bit ESP32, `size_t == unsigned int` causes overload conflicts.

**USB MIDI pipeline:**
1. `USB_HOST_CLIENT_EVENT_NEW_DEV` → open device, scan config descriptor for MIDI streaming interface (class=0x01, subclass=0x03) + bulk IN/OUT endpoints
2. Claim interface, allocate bulk IN transfer, submit continuously
3. `midi_bulk_in_cb` → parse 4-byte USB MIDI packets → `dispatchMidiReceived` → `_receiveCallback` → `routingManager.routeMidiMessage` → `sendMidi` → bulk OUT transfer
4. `USB_HOST_CLIENT_EVENT_DEV_GONE` → `dev_gone.dev_hdl` (not address) → teardown

**Display:** LovyanGFX with sprite double-buffering. All draw methods write to `_sprite`, then `draw()` calls `_sprite.pushSprite(&display, 0, 0)` once. This eliminates flicker.

**NVS storage:** Uses ESP-IDF NVS directly (`nvs_open`, `nvs_set_str`, etc.), not Arduino `Preferences`. Namespace `"midi_router"`.

## USB hub topology

Tested with USB-C hub → USB-A hub daisy-chain. MIDI devices must be on the USB-A hub. USB 3.x USB-C hub ports enumerate unexpectedly (likely due to internal USB 2.0 companion hub topology); USB-A hub ports work reliably. The USB-C hub must be externally powered.

Some devices (e.g. Sequential OB-6) may fail enumeration silently. Diagnostic USB log is visible at C → Settings. The log shows all USB events including `new_dev aX`, descriptor errors, and `aX clsYY name` entries.

## File map

| File | Purpose |
|------|---------|
| `src/main.cpp` | `app_main`, init sequence, callbacks |
| `src/MidiDevice.cpp` | USB host, enumeration, bulk transfers, MIDI dispatch |
| `src/Routing.cpp` | Routing table, channel filter, message routing |
| `src/Storage.cpp` | NVS persistence |
| `src/UI.cpp` | Sprite-buffered display, keyboard input handling |
| `src/Cardputer.cpp` | LovyanGFX display driver, keyboard GPIO matrix |
| `include/arduino_compat.h` | String/millis/delay shims for ESP-IDF builds |
| `sdkconfig.m5stack-cardputer` | Live Kconfig — edit this for USB/FreeRTOS settings |
| `default_8MB.csv` | Partition table (copied from Arduino framework) |
