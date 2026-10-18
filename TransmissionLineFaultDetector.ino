// ============================================================
//  SMART TRANSMISSION LINE FAULT DETECTION SYSTEM
//  ESP32 + ACS712 + ZMPT101B + ADS1115
// ------------------------------------------------------------
//  A 12V AC model transmission line (step-down transformer +
//  bulb load) is monitored continuously. The firmware measures
//  line voltage (ZMPT101B) and load current (ACS712) through an
//  ADS1115 ADC, classifies the line condition, and reports it
//  on a 16x2 LCD, status LEDs, a buzzer, the Serial Monitor and
//  a Wi-Fi status page.
//
//  FAULTS DETECTED FROM SENSOR DATA:
//    1. SHORT CIRCUIT  - current far above the trip threshold
//    2. OVERLOAD       - current above rated value, sustained
//    3. OPEN CIRCUIT   - line voltage present but no load current
//                        (line break / disconnected load)
//    4. LINE DOWN      - line voltage lost (supply-side failure)
//
//  Four push buttons simulate (trigger) each fault for
//  demonstration, as per the project design. A short circuit
//  LATCHES until the RESET button is pressed, mirroring real
//  protection practice (a breaker does not reclose into a
//  short on its own).
//
//  Author : Deepesh Chauhan, B.Tech Electrical Engineering
//           RGGEC Kangra
// ============================================================

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_ADS1X15.h>

// ------------------------------------------------------------
//  WI-FI CONFIG  (replace with your own network credentials)
// ------------------------------------------------------------
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// ------------------------------------------------------------
//  PIN DEFINES
// ------------------------------------------------------------
#define BUZZER_PIN     27
#define LED_GREEN_PIN  14   // line healthy
#define LED_RED_PIN    12   // fault

#define BTN_SHORT      32   // simulate short circuit
#define BTN_OPEN       33   // simulate open circuit (line break)
#define BTN_OVERLOAD   25   // simulate overload
#define BTN_RESET      26   // reset / acknowledge

// ADS1115 channels
#define ADS_CH_VOLTAGE 0    // ZMPT101B output
#define ADS_CH_CURRENT 1    // ACS712 output

// ------------------------------------------------------------
//  SENSOR CALIBRATION
// ------------------------------------------------------------
// ZMPT101B: module output is an AC waveform centred near
// mid-supply. VOLT_CAL converts measured RMS volts at the ADC
// pin into actual line volts. Calibrate once against a
// multimeter on the 12V line and adjust if needed.
const float VOLT_CAL        = 19.5f;   // line volts per ADC RMS volt

// ACS712 (5A version): 185 mV per amp, zero-current output at
// half the supply (2.5V with a 5V supply; the ADS1115 reads
// the divided signal, so keep the divider ratio in CUR_CAL).
const float ACS_SENSITIVITY = 0.185f;  // V per A
const float CUR_CAL         = 1.0f;    // divider correction factor

// ------------------------------------------------------------
//  LINE RATINGS & FAULT THRESHOLDS  (12V model line)
// ------------------------------------------------------------
const float LINE_VOLTAGE_NOMINAL = 12.0f;   // V (transformer output)
const float RATED_CURRENT        = 0.85f;   // A (12V / 10W bulb load)

const float SHORT_CIRCUIT_A      = 1.70f;   // > 2.0 x rated  -> SC
const float OVERLOAD_A           = 1.15f;   // > 1.35 x rated -> OL
const float OPEN_CIRCUIT_A       = 0.05f;   // < this = no load current
const float LINE_DOWN_V          = 2.0f;    // < this = supply lost
const float LINE_PRESENT_V       = 8.0f;    // > this = line energised

const unsigned long OVERLOAD_CONFIRM_MS = 1500;  // OL must persist
const unsigned long OPEN_CONFIRM_MS     = 1000;  // OC must persist
const unsigned long SIM_FAULT_MS        = 10000; // simulated fault hold

// RMS sampling window
const int RMS_SAMPLES = 200;

// ------------------------------------------------------------
//  OBJECTS & STATE
// ------------------------------------------------------------
Adafruit_ADS1115 ads;
LiquidCrystal_I2C lcd(0x27, 16, 2);
WebServer server(80);

enum FaultType { F_NONE, F_SHORT, F_OPEN, F_OVERLOAD, F_LINEDOWN };

FaultType activeFault   = F_NONE;
bool      faultLatched  = false;       // set only by short circuit
FaultType simFault      = F_NONE;      // button-simulated fault
unsigned long simFaultUntil = 0;

float lineVoltage = 0.0f;              // RMS volts
float lineCurrent = 0.0f;              // RMS amps

unsigned long overloadSince = 0;
unsigned long openSince     = 0;
unsigned long lastMeasure   = 0;
unsigned long lastLcdUpdate = 0;

String eventLog = "System boot\n";

// ------------------------------------------------------------
//  HELPERS
// ------------------------------------------------------------
const char* faultName(FaultType f) {
  switch (f) {
    case F_SHORT:    return "SHORT CIRCUIT";
    case F_OPEN:     return "OPEN CIRCUIT";
    case F_OVERLOAD: return "OVERLOAD";
    case F_LINEDOWN: return "LINE DOWN";
    default:         return "LINE OK";
  }
}

void logEvent(const String& msg) {
  eventLog += msg + "\n";
  if (eventLog.length() > 1500) eventLog = eventLog.substring(500);
  Serial.println(msg);
}

void beep(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(onMs);
    digitalWrite(BUZZER_PIN, LOW);
    if (i < times - 1) delay(offMs);
  }
}

// RMS of one ADS1115 channel over a sampling window.
// The sensor outputs are AC waveforms biased at mid-supply,
// so RMS is computed after removing the window mean (DC bias).
float readChannelRMS(int channel) {
  long sum = 0;
  int  n   = RMS_SAMPLES;
  int16_t raw[ RMS_SAMPLES ];

  for (int i = 0; i < n; i++) {
    raw[i] = ads.readADC_SingleEnded(channel);
    sum += raw[i];
    delayMicroseconds(800);            // ~50 samples per 50Hz cycle span
  }
  float mean = (float)sum / n;

  double sq = 0;
  for (int i = 0; i < n; i++) {
    float d = (float)raw[i] - mean;
    sq += (double)d * d;
  }
  float rmsCounts = sqrt(sq / n);
  return ads.computeVolts(rmsCounts);  // RMS volts at the ADC pin
}

void measureLine() {
  float vAdc = readChannelRMS(ADS_CH_VOLTAGE);
  float iAdc = readChannelRMS(ADS_CH_CURRENT);

  lineVoltage = vAdc * VOLT_CAL;
  lineCurrent = (iAdc / ACS_SENSITIVITY) * CUR_CAL;
  if (lineCurrent < 0.02f) lineCurrent = 0.0f;   // noise floor
}

// ------------------------------------------------------------
//  FAULT CLASSIFICATION  (sensor-driven)
// ------------------------------------------------------------
FaultType classifyFromSensors() {
  unsigned long now = millis();

  // Supply lost entirely
  if (lineVoltage < LINE_DOWN_V) return F_LINEDOWN;

  // Severe overcurrent reacts instantly
  if (lineCurrent > SHORT_CIRCUIT_A) return F_SHORT;

  // Overload must persist before tripping
  if (lineCurrent > OVERLOAD_A) {
    if (overloadSince == 0) overloadSince = now;
    if (now - overloadSince >= OVERLOAD_CONFIRM_MS) return F_OVERLOAD;
  } else {
    overloadSince = 0;
  }

  // Line energised but load drawing nothing -> break in the line
  if (lineVoltage > LINE_PRESENT_V && lineCurrent < OPEN_CIRCUIT_A) {
    if (openSince == 0) openSince = now;
    if (now - openSince >= OPEN_CONFIRM_MS) return F_OPEN;
  } else {
    openSince = 0;
  }

  return F_NONE;
}

void updateFaultState() {
  FaultType detected = classifyFromSensors();

  // A latched short circuit holds until manual RESET,
  // exactly like a tripped protection relay.
  if (faultLatched) {
    activeFault = F_SHORT;
    return;
  }

  // Button simulation (demo mode) overrides the sensor verdict
  if (simFault != F_NONE && millis() < simFaultUntil) {
    detected = simFault;
  }

  if (detected != activeFault) {
    if (detected != F_NONE) {
      logEvent(String("FAULT: ") + faultName(detected) +
               "  V=" + String(lineVoltage, 1) +
               "V  I=" + String(lineCurrent, 2) + "A");
      if (detected == F_SHORT) {
        faultLatched = true;
        beep(4, 120, 80);
      } else {
        beep(2, 150, 100);
      }
    } else {
      logEvent("Line restored: LINE OK");
      beep(1, 80, 0);
    }
    activeFault = detected;
  }
}

void updateOutputs() {
  bool healthy = (activeFault == F_NONE);
  digitalWrite(LED_GREEN_PIN, healthy ? HIGH : LOW);
  digitalWrite(LED_RED_PIN,   healthy ? LOW  : HIGH);
  // Continuous slow alarm while a fault is active
  if (!healthy && (millis() / 1000) % 2 == 0) {
    digitalWrite(BUZZER_PIN, HIGH);
  } else if (!healthy) {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("V:");
  lcd.print(lineVoltage, 1);
  lcd.print("V I:");
  lcd.print(lineCurrent, 2);
  lcd.print("A");
  lcd.setCursor(0, 1);
  if (activeFault == F_NONE) {
    lcd.print("Status: LINE OK ");
  } else {
    lcd.print("FAULT:");
    lcd.print(faultName(activeFault));
  }
}

// ------------------------------------------------------------
//  BUTTON HANDLING  (simulate faults + reset)
// ------------------------------------------------------------
bool pressed(int pin) { return digitalRead(pin) == LOW; }  // INPUT_PULLUP

void handleButtons() {
  if (pressed(BTN_RESET)) {
    if (faultLatched || activeFault != F_NONE || simFault != F_NONE) {
      faultLatched = false;
      simFault     = F_NONE;
      activeFault  = F_NONE;
      overloadSince = 0;
      openSince     = 0;
      logEvent("RESET pressed: protection reset by operator");
      beep(1, 60, 0);
    }
    delay(250);  // debounce
  }
  if (pressed(BTN_SHORT)) {
    simFault = F_SHORT; simFaultUntil = millis() + SIM_FAULT_MS;
    faultLatched = true;
    logEvent("SIMULATION: short-circuit button pressed");
    delay(250);
  }
  if (pressed(BTN_OPEN)) {
    simFault = F_OPEN; simFaultUntil = millis() + SIM_FAULT_MS;
    logEvent("SIMULATION: open-circuit button pressed");
    delay(250);
  }
  if (pressed(BTN_OVERLOAD)) {
    simFault = F_OVERLOAD; simFaultUntil = millis() + SIM_FAULT_MS;
    logEvent("SIMULATION: overload button pressed");
    delay(250);
  }
}

// ------------------------------------------------------------
//  WI-FI STATUS PAGE
// ------------------------------------------------------------
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta http-equiv='refresh' content='3'>"
    "<title>Transmission Line Monitor</title>"
    "<style>body{font-family:Arial;background:#0f172a;color:#e2e8f0;"
    "text-align:center;padding:2rem}h1{color:#38bdf8}.card{background:#1e293b;"
    "border-radius:12px;padding:1.5rem;max-width:420px;margin:auto}"
    ".ok{color:#4ade80}.bad{color:#f87171}pre{text-align:left;white-space:"
    "pre-wrap}</style></head><body><div class='card'>"
    "<h1>Transmission Line Monitor</h1>";
  html += "<p>Line Voltage: <b>" + String(lineVoltage, 1) + " V</b></p>";
  html += "<p>Load Current: <b>" + String(lineCurrent, 2) + " A</b></p>";
  html += "<p>Status: <b class='" + String(activeFault == F_NONE ? "ok" : "bad") +
          "'>" + faultName(activeFault) + "</b></p>";
  html += "<h3>Event Log</h3><pre>" + eventLog + "</pre>";
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

// ------------------------------------------------------------
//  SETUP & LOOP
// ------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(BTN_SHORT, INPUT_PULLUP);
  pinMode(BTN_OPEN, INPUT_PULLUP);
  pinMode(BTN_OVERLOAD, INPUT_PULLUP);
  pinMode(BTN_RESET, INPUT_PULLUP);

  Wire.begin(21, 22);                       // ESP32 default I2C
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("TransmissionLine");
  lcd.setCursor(0, 1);
  lcd.print("Fault Detector  ");

  if (!ads.begin()) {
    Serial.println("ADS1115 not found - check wiring!");
    lcd.setCursor(0, 1);
    lcd.print("ADS1115 ERROR!  ");
    while (true) delay(1000);
  }
  ads.setGain(GAIN_ONE);                    // +/- 4.096V range

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 8000) {
    delay(300);
  }
  if (WiFi.status() == WL_CONNECTED) {
    server.on("/", handleRoot);
    server.begin();
    logEvent("Wi-Fi connected. Status page: http://" +
             WiFi.localIP().toString());
  } else {
    logEvent("Wi-Fi not connected - continuing offline");
  }

  beep(1, 100, 0);
  logEvent("System ready. Monitoring line...");
  delay(1200);
}

void loop() {
  server.handleClient();
  handleButtons();

  if (millis() - lastMeasure >= 250) {
    lastMeasure = millis();
    measureLine();
    updateFaultState();
    updateOutputs();

    Serial.print("V=");  Serial.print(lineVoltage, 1);
    Serial.print("V  I="); Serial.print(lineCurrent, 2);
    Serial.print("A  Status: "); Serial.println(faultName(activeFault));
  }

  if (millis() - lastLcdUpdate >= 500) {
    lastLcdUpdate = millis();
    updateLCD();
  }
}
