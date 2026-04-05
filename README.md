# Midiputer

A USB MIDI router/hub for the M5Stack Cardputer. Connects multiple USB MIDI devices through a hub and routes MIDI messages between them with per-channel filtering, persistent routing configuration, and a live display.

## Hardware

- **M5Stack Cardputer** (ESP32-S3)
- **Powered USB hub** (required — the Cardputer's USB port cannot power devices on its own)
- USB MIDI devices (keyboards, synths, etc.)

> A USB-C hub daisy-chained with a USB-A hub works well. Plug MIDI devices into the USB-A hub. The USB-C hub must be externally powered.

## Features

- **USB MIDI host** — enumerate and communicate with USB MIDI class devices
- **Flexible routing** — route any input device to any output device
- **Channel filtering** — per-routing 16-channel filter (all, none, or individual channels)
- **Persistent storage** — routings and device names saved to NVS flash
- **Live MIDI activity** — routing rows flash yellow when messages pass through
- **Flicker-free display** — sprite double-buffering for smooth updates
- **Keyboard navigation** — full UI on the Cardputer's built-in keyboard

## Controls

| Key | Action |
|-----|--------|
| W / S | Navigate up / down |
| N | New routing |
| E / Enter | Edit selected routing |
| D | Delete selected routing |
| C | Settings / USB log |
| Q / Backspace | Back / Cancel |
| A / D | Navigate left / right (channel select) |
| Space / Enter | Toggle channel (channel select) |
| A | Toggle all channels (channel select) |
| Y / N | Confirm / cancel delete |

## Building

Requires [PlatformIO](https://platformio.org/) and Python 3.11+.

```sh
pio run
pio run -t upload
```

To flash, hold **Boot**, tap **Reset**, release **Boot** to enter bootloader mode, then run upload.

## Architecture

```
src/
  main.cpp          — app_main, USB host init, callback wiring
  MidiDevice.cpp    — USB host enumeration, bulk IN/OUT transfers, MIDI dispatch
  Routing.cpp       — routing table, channel filter, message routing logic
  Storage.cpp       — NVS persistence for routings and device names
  UI.cpp            — display rendering (sprite double-buffered), keyboard input
  Cardputer.cpp     — LovyanGFX display driver, GPIO keyboard matrix scanner

include/
  MidiDevice.h      — MidiDevice, MidiDeviceManager
  MidiTypes.h       — MidiMessage, MidiMessageType, ChannelFilter
  Routing.h         — MidiRouting, RoutingManager
  Storage.h         — StorageManager (NVS wrapper)
  UI.h              — UIManager, Colors, screen/field enums
  Cardputer.h       — LGFX_Cardputer, CardputerKeyboard
  arduino_compat.h  — String class, millis(), delay() shims for ESP-IDF
```

## Framework notes

Uses `framework = espidf` (not Arduino) via `espressif32` platform (ESP-IDF 5.5.3). This is required to enable `CONFIG_USB_HOST_HUBS_SUPPORTED` for external USB hub enumeration — the pre-compiled Arduino-ESP32 SDK does not expose this option.

The ESP-IDF 5.5.3 Python tooling requires Python ≥ 3.11 (specifically, `cryptography >= 44.0.0` excludes Python 3.9.0). The PlatformIO virtualenv at `~/.platformio/penv/.espidf-5.5.3/` must be created with Python 3.11.
