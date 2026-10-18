# Components — Smart Transmission Line Fault Detection System

| S.No. | Component | Purpose | Qty |
| --- | --- | --- | --- |
| 1 | ESP32 DevKit V1 (30-pin) | Main controller (brain) with Wi-Fi for IoT | 1 |
| 2 | ACS712 Current Sensor | Measures current to detect short circuits | 1 |
| 3 | ZMPT101B Voltage Sensor | Measures voltage to detect line breaks | 1 |
| 4 | ADS1115 ADC Module | Accurate analog-to-digital conversion | 1 |
| 5 | 16×2 I2C LCD Display | Visual status indicator (Line OK / Fault) | 1 |
| 6 | Active Buzzer Module | Sound alert on fault detection | 1 |
| 7 | Push Buttons (Tactile) | Simulate (trigger) faults | 4 |
| 8 | Breadboard & Jumper Wires | Prototyping without soldering | 1 |
| 9 | LED & Resistor Kit | Visual indicators (Green / Red) + protection | 1 |
| 10 | 12V 1A Step-down Transformer | Simulates a safe AC transmission line | 1 |
| 11 | 12V Bulb / Small Load | Load so current can flow for sensing | 1 |
| 12 | 5V 2A Power Adapter | Stable DC power for ESP32 & sensors | 1 |
| 13 | Micro-USB Data Cable | Uploading code to the ESP32 | 1 |

**Safety note:** the "transmission line" in this project is a 12V AC model line fed by a step-down transformer — all sensing and fault simulation happen at safe low voltage.
