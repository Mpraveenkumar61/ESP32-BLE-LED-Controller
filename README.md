ESP32 BLE LED Controller

Control an onboard LED over Bluetooth Low Energy from your iPhone using the **nRF Connect** app. Send text commands via a custom GATT service and receive acknowledgement responses in real time.

---

## Overview

This project runs on an **ESP32** using the ESP-IDF framework with the Bluedroid BLE stack. It exposes a custom GATT service with two characteristics — one for receiving commands from the phone (write) and one for sending responses back (notify). The ESP32 advertises itself as `ESP32-praveen` and is ready to pair with any BLE scanner like nRF Connect on iOS.

---

## Features

- BLE GATT server with custom RX (write) and TX (notify) characteristics
- LED on/off control via text commands
- Blink, status query, and uptime reporting
- Auto-restart advertising after disconnect
- Periodic uptime broadcast every 2 seconds while connected
- No pairing or bonding required

---

## Hardware Requirements

| Component | Details |
|-----------|---------|
| Board | ESP32 (any variant with onboard LED) |
| LED | Onboard LED on GPIO 2 |
| Power | USB or 3.3V supply |

No external wiring needed — the onboard LED on GPIO 2 is used by default.

---

## Software Requirements

- [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [nRF Connect for iOS](https://apps.apple.com/app/nrf-connect-for-mobile/id1054903597) (free, App Store)
- Python 3.8+ (for idf.py toolchain)

---

## Project Structure
esp32-ble-led/
├── main/
│   └── main.c          # Full application source
├── CMakeLists.txt       # Top-level CMake
├── main/CMakeLists.txt  # Component CMake
└── README.md

---

## BLE Architecture

| Role | UUID | Permission | Description |
|------|------|------------|-------------|
| Service | `0x0001` | — | Custom GATT service |
| RX Characteristic | `0x0002` | Write / Write-NR | Phone → ESP32 commands |
| TX Characteristic | `0x0003` | Notify | ESP32 → Phone responses |
| CCCD | `0x2902` | Read / Write | Enable notifications |

---

## Build and Flash

```bash
# 1. Set up ESP-IDF environment
. $HOME/esp/esp-idf/export.sh

# 2. Clone this repo
git clone https://github.com/Mpraveenkumar61/ESP32-BLE-LED-Controller.git
cd ESP32-BLE-LED-Controller

# 3. Build
idf.py build

# 4. Flash (replace PORT with your serial port)
idf.py -p PORT flash

# 5. Monitor serial output
idf.py -p PORT monitor
```

Expected serial output after flashing:
========================================
ESP32 BLE UART — iOS Compatible
Device: ESP32-praveen
App:    nRF Connect (App Store, free)
I (xxx) BLE_LED: BLE advertising started

---

## nRF Connect Usage (iOS)

1. Install **nRF Connect** from the App Store
2. Open the app and go to the **Scanner** tab
3. Find `ESP32-praveen` in the list and tap **Connect**
4. Go to the **Services** tab
5. Find the service with UUID `0x0001`
6. Tap the **TX characteristic** (`0x0003`) and tap the **bell/subscribe icon** to enable notifications
7. Tap the **RX characteristic** (`0x0002`) and tap the **up-arrow (write) icon**
8. Select **Text (UTF-8)**, type a command like `ON`, and tap **Send**

Responses from the ESP32 will appear live under the TX characteristic value.

---

## Command Reference

| Command | Action | Response |
|---------|--------|----------|
| `ON` | Turn LED on | `ACK:ON` |
| `OFF` | Turn LED off | `ACK:OFF` |
| `STATUS` | Query LED state | `LED:ON` or `LED:OFF` |
| `HELLO` | Ping the device | `Hi from ESP32-praveen!` |
| `BLINK` | Blink LED 3 times | `ACK:BLINK` |
| `UPTIME` | Get uptime in seconds | `UPTIME:42s` |
| *(anything else)* | Unknown command | `ERR:UNKNOWN` |

On connecting and enabling notifications, the device automatically sends:
ESP32-praveen Connected!
Cmds: LED_ON LED_OFF STATUS HELLO BLINK UPTIME

---

## Configuration

All configurable values are at the top of `main/main.c`:

```c
#define DEVICE_NAME   "ESP32-praveen"   // BLE advertised name
#define LED_PIN       GPIO_NUM_2        // GPIO for LED
```

Change `DEVICE_NAME` to rename the device, or `LED_PIN` for a different GPIO. Re-flash after any change.

---

## Troubleshooting

**Device not showing in scanner**
- Grant Bluetooth and Location permissions to nRF Connect in iOS Settings
- Press the reset button on the ESP32 and scan again

**Notifications not arriving**
- You must tap the subscribe/bell icon on the TX characteristic before sending commands
- Only one phone can be connected at a time

**LED not responding**
- Confirm your board has an LED on GPIO 2 — some boards use a different pin
- Check the serial monitor for `CMD:` log lines to confirm commands are received

**Build errors**
- Ensure you are using ESP-IDF v5.x — v4.x has a different GATTS API
- Run `idf.py fullclean` then rebuild

---

## Author

**Praveen Kumar M**
GitHub: [@Mpraveenkumar61](https://github.com/Mpraveenkumar61)

---

## License

MIT License — free to use and modify for personal and commercial projects.
