# ESP32 Automatic Sanitizer IoT

## Description
An automatic sanitizer dispenser controlled by an ESP32. An IR sensor detects a hand, a servo presses the pump once, and live status is shown on a web dashboard.

## Features
- Touch-free dispensing, one dispense per hand (anti-repeat and cooldown)
- Wi-Fi + MQTT status reporting (online/offline, sensor, servo, counter, last dispense)
- Web dashboard on GitHub Pages, with manual dispense and reset counter
- Works offline; the counter survives power loss
- Easily adjustable settings at the top of the firmware

## Components
ESP32 DevKit, SG90 servo, IR obstacle sensor (FC-51 type), sanitizer pump, 5V 2A supply, 470-1000 uF capacitor, breadboard, jumper wires.

## Pin Configuration
| Component | ESP32 |
|---|---|
| Servo signal | GPIO 13 |
| IR sensor OUT | GPIO 27 |
| IR VCC | 3V3 |
| Servo VCC | External 5V |
| All GND | Common GND |

## Wiring
See `wiring/pinout.md`. The servo is powered from an external 5V supply; **all grounds must be connected together.**

## Software Requirements
Arduino IDE 2.x; ESP32 board package (Espressif); libraries **PubSubClient** (Nick O'Leary) and **ESP32Servo** (Kevin Harrington / John K. Bennett). `WiFi`, `Preferences` and `time.h` are built in.

## Arduino IDE Setup
Add the boards URL `https://espressif.github.io/arduino-esp32/package_esp32_index.json` in Preferences, install "esp32 by Espressif Systems" in Boards Manager, then choose **ESP32 Dev Module**. Install PubSubClient and ESP32Servo in Library Manager.

## ESP32 Setup
Open `firmware/sanitizer_esp32/sanitizer_esp32.ino`. Select the board and port. Serial Monitor: 115200 baud.

## Wi-Fi Configuration
Copy `secrets.example.h` to `secrets.h` in the same folder and set `WIFI_SSID`, `WIFI_PASSWORD` and `DEVICE_ID`. `secrets.h` is git-ignored: never upload it. The ESP32 supports 2.4 GHz Wi-Fi only.

## IoT Configuration
MQTT via the free public broker `broker.hivemq.com`. Topics: `sanitizer-iot/<DEVICE_ID>/state`, `/online`, `/cmd`. No account needed. The public broker is not private, so use a long random `DEVICE_ID`.

## How It Works
IDLE -> hand detected -> servo presses (DISPENSE_TIME) -> servo returns home -> cooldown -> wait for hand removal -> IDLE.

## Web Dashboard
Enable GitHub Pages (Settings -> Pages -> Branch: main, Folder: /docs), then open:
`https://<username>.github.io/ESP32-Automatic-Sanitizer-IoT/#<DEVICE_ID>`

## Installation
1. Wire the hardware. 2. Create `secrets.h`. 3. Upload the firmware. 4. Open the dashboard URL with your Device ID.

## Testing
Test in this order: Serial boot, IR sensor, servo, one dispense per hand, anti-repeat, Wi-Fi reconnect, dashboard, manual button.

## Troubleshooting
- Servo not moving: check external 5V and the common GND.
- ESP32 restarts: power the servo separately and add the capacitor.
- IR always/never detecting: adjust the potentiometer or flip `OBJECT_DETECTED_STATE`.
- Dashboard stays offline: Device ID mismatch, or the ESP32 is not connected to the broker.
- Compile error about Servo: install **ESP32Servo**, not the AVR `Servo` library.

## Project Structure
```
firmware/sanitizer_esp32/  (sanitizer_esp32.ino, secrets.example.h)
docs/                      (index.html, style.css, script.js)
wiring/pinout.md
README.md
.gitignore
```

## Future Improvements
Private broker with login, an ultrasonic sanitizer-level sensor, low-level alert, dispense history chart, OTA updates, and a 3D-printed housing.

## Authors
- Your Name, Course/Section, School (add your group members)
