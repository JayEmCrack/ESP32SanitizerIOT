# Pinout

| Component | Pin | Connects to |
|---|---|---|
| Servo signal | Signal | ESP32 GPIO 13 |
| Servo VCC | VCC | External 5V (+) |
| Servo GND | GND | External 5V (-) AND ESP32 GND |
| IR OUT | OUT | ESP32 GPIO 27 |
| IR VCC | VCC | ESP32 3V3 |
| IR GND | GND | ESP32 GND |
| 470-1000 uF capacitor | + / - | Servo 5V / Servo GND (near the servo) |
| ESP32 power | USB | Computer / 5V adapter |

## Wiring diagram

```
ESP32
├── GPIO 13 ───────────────► Servo Signal (orange)
├── GPIO 27 ◄───────────────  IR OUT
├── 3V3 ───────────────────► IR VCC
├── GND ───────┬───────────── IR GND
│              └───────────── COMMON GND ◄─ External 5V (-)
├── USB ◄── computer / 5V adapter (powers ESP32 only)
└── Wi-Fi ~~~~► router ► internet ► MQTT broker ► Dashboard

External 5V 2A supply
├── (+) ──┬──► Servo VCC (red)
│         └──► 470-1000 uF cap (+)
└── (-) ──┬──► Servo GND (brown)
          ├──► cap (-)
          └──► ESP32 GND  (COMMON GROUND)
```

## Rules
- Never power the servo from the ESP32 3V3 pin. Servo current spikes can reset the ESP32 (brownout).
- Connect all grounds together (common GND), otherwise the servo signal is unreliable.
- Power the IR sensor at 3.3V. If a 5V-only module is used, divide OUT with 10k (OUT to GPIO27) and 20k (GPIO27 to GND) to get about 3.3V, or use a level shifter.
- GPIO 27 is used because it is not a strapping, flash or input-only pin.
- Do not connect USB and a 5V source to the ESP32 VIN at the same time.

## Servo calibration
Start with SERVO_DISPENSE = 30, then increase slowly until the pump is fully pressed. Stop before the servo strains or hums against a mechanical limit. Set SERVO_MAX_SAFE just above that value.
