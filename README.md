# Smart Transmission Line Fault Detection System 🔌

**ESP32-based detection and classification of faults on a model transmission line, with LCD, buzzer, LED and Wi-Fi status reporting.**

Real transmission lines face short circuits, line breaks and overloads — and the faster a fault is identified, the faster it can be isolated. This project builds the same protection idea at a safe scale: a **12V AC model line** (step-down transformer + bulb load) is monitored in real time, faults are detected from actual sensor data, and four push buttons can also simulate each fault for demonstration.

## ⚠️ Faults detected

| Fault | How it is detected |
| --- | --- |
| **Short Circuit** | Load current rises above 2× rated value (instant trip, latches until RESET) |
| **Overload** | Current above 1.35× rated value, sustained beyond the confirmation time |
| **Open Circuit (line break)** | Line voltage present but load current falls to ~zero |
| **Line Down** | Line voltage itself is lost (supply-side failure) |

## 🔍 How it works

1. **ZMPT101B** senses line voltage and **ACS712** senses load current, both read precisely through an **ADS1115** ADC
2. The ESP32 computes true RMS values and classifies the line condition every 250 ms
3. Status is shown on a **16×2 LCD** ("V / I / LINE OK or FAULT: …"), with a **green/red LED** pair and **buzzer** alarm
4. A **Wi-Fi status page** served by the ESP32 shows live voltage, current, fault state and an event log
5. Short circuits **latch** like a real protection relay — an operator RESET is required

## 📂 In this repo

- `TransmissionLineFaultDetector.ino` — complete firmware
- `components.md` — full component list with the role of each part

## 🛠️ Hardware

ESP32 DevKit V1 · ACS712 current sensor · ZMPT101B voltage sensor · ADS1115 ADC · 16×2 I2C LCD · buzzer · fault-simulation push buttons · 12V step-down transformer (safe model line)

## 🔐 Note

Wi-Fi credentials in the firmware are placeholders — add your own before uploading.

## 👤 Author

**Deepesh Chauhan** — B.Tech Electrical Engineering, RGGEC Kangra
[LinkedIn](https://www.linkedin.com/in/deepeshhh1)
