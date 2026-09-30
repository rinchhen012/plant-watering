// ============================================================
//  Plant Watering System  (Arduino Uno)
//  Soil-moisture-only control: waters when soil drops below a
//  threshold, waits through a cooldown so water can soak in.
//
//  Serial commands (9600 baud):
//    help          - list commands
//    read          - one live raw + % reading
//    stat          - status overview
//    water         - water now (manual)
//    set 35        - set moisture threshold to 35%
//    cal           - calibration wizard (dry + wet)
// ============================================================

#include <EEPROM.h>

// ---------- Pin config ----------
const uint8_t PIN_SENSOR_POWER = 4;   // feeds 5V to sensor; can be cut off
const uint8_t PIN_SENSOR_AO    = A0;  // analog output of capacitive sensor
const uint8_t PIN_PUMP         = 7;   // relay/MOSFET signal (active HIGH)
const uint8_t PIN_BUTTON       = 2;   // manual water button (INPUT_PULLUP)
const uint8_t PIN_LED          = 13;  // status LED: on while pumping
const uint8_t PIN_BUZZER       = 8;   // beeps on faults / calibration steps

// ---------- Behaviour ----------
const uint32_t PUMP_ON_MS      = 5000UL;            // 5 s of water per watering
const uint32_t MAX_PUMP_RUN_MS = 30000UL;           // safety cap, never exceed
const uint32_t COOLDOWN_MS     = 6UL * 3600UL * 1000UL; // 6 h soak-in pause
const uint16_t READ_INTERVAL_MS = 60000UL;          // check soil every minute
const uint8_t  SAMPLES         = 10;                // averaged analog reads
const uint8_t  DEFAULT_THRESHOLD = 30;              // water below 30%

// ---------- EEPROM layout ----------
const uint8_t EEP_MAGIC      = 0;  // marker that calibration data is valid
const uint8_t EEP_DRY_HI     = 1;  // raw value in dry soil/air
const uint8_t EEP_WET_HI     = 3;  // raw value in water
const uint8_t EEP_THRESHOLD  = 5;  // moisture % at which we water
const uint8_t EEP_MAGIC_VAL  = 0x5A;

// ---------- State ----------
enum State { IDLE, PUMPING, COOLDOWN };
State state = IDLE;

uint16_t calDry = 800;   // raw value when bone dry (pre-calibration guess)
uint16_t calWet = 400;   // raw value when soaked (pre-calibration guess)
uint8_t  threshold = DEFAULT_THRESHOLD;
bool     calibrated = false;

uint32_t lastReadMs   = 0;
uint32_t lastChangeMs = 0;   // when the last watering finished

// ---------- EEPROM helpers ----------
void writeWord(uint8_t addr, uint16_t value) {
  EEPROM.write(addr,     highByte(value));
  EEPROM.write(addr + 1, lowByte(value));
}

uint16_t readWord(uint8_t addr) {
  return (uint16_t)EEPROM.read(addr) << 8 | EEPROM.read(addr + 1);
}

void loadSettings() {
  if (EEPROM.read(EEP_MAGIC) == EEP_MAGIC_VAL) {
    calDry      = readWord(EEP_DRY_HI);
    calWet      = readWord(EEP_WET_HI);
    threshold   = EEPROM.read(EEP_THRESHOLD);
    calibrated  = true;
  }
}

void saveSettings() {
  writeWord(EEP_DRY_HI, calDry);
  writeWord(EEP_WET_HI, calWet);
  EEPROM.write(EEP_THRESHOLD, threshold);
  EEPROM.write(EEP_MAGIC, EEP_MAGIC_VAL);
  calibrated = true;
}

// ---------- Sensor ----------
uint16_t readSensorRaw() {
  // Power the sensor only during the measurement to avoid
  // electrolysis drift and reduce corrosion.
  digitalWrite(PIN_SENSOR_POWER, HIGH);
  delay(300);  // let the sensor settle
  uint32_t sum = 0;
  for (uint8_t i = 0; i < SAMPLES; i++) {
    sum += analogRead(PIN_SENSOR_AO);
    delay(20);
  }
  digitalWrite(PIN_SENSOR_POWER, LOW);
  return sum / SAMPLES;
}

// raw -> 0..100% moisture. 100% = as wet as calibration water.
int moisturePercent(uint16_t raw) {
  if (calDry <= calWet) return 50;  // bad calibration; keep it neutral
  long pct = 100L * (calDry - raw) / (calDry - calWet);
  return constrain((int)pct, 0, 100);
}

// ---------- Pump ----------
void pumpOn()  { digitalWrite(PIN_PUMP, HIGH); digitalWrite(PIN_LED, HIGH); }
void pumpOff() { digitalWrite(PIN_PUMP, LOW);  digitalWrite(PIN_LED, LOW);  }

void beep(uint8_t times, uint16_t ms) {
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(ms);
    digitalWrite(PIN_BUZZER, LOW);
    delay(ms);
  }
}

// ---------- Serial ----------
char line[32];
uint8_t lineLen = 0;

void processCommand(char *cmd) {
  char *arg = strchr(cmd, ' ');
  if (arg) *arg++ = '\0';

  if (strcmp(cmd, "help") == 0) {
    Serial.println(F("commands: help, read, stat, water, set <0-100>, cal"));
  } else if (strcmp(cmd, "read") == 0) {
    uint16_t raw = readSensorRaw();
    Serial.print(F("raw=")); Serial.print(raw);
    Serial.print(F("  moisture=")); Serial.print(moisturePercent(raw));
    Serial.println(F("%"));
  } else if (strcmp(cmd, "stat") == 0) {
    uint16_t raw = readSensorRaw();
    Serial.print(F("state="));
    Serial.print(state == IDLE ? F("IDLE") : state == PUMPING ? F("PUMPING") : F("COOLDOWN"));
    Serial.print(F("  raw=")); Serial.print(raw);
    Serial.print(F("  moisture=")); Serial.print(moisturePercent(raw));
    Serial.print(F("%  threshold=")); Serial.print(threshold);
    Serial.print(F("%  calibrated=")); Serial.println(calibrated ? F("yes") : F("NO"));
  } else if (strcmp(cmd, "water") == 0) {
    if (state == PUMPING) { Serial.println(F("already pumping")); return; }
    Serial.println(F("manual watering"));
    pumpOn();
    state = PUMPING;
    lastChangeMs = millis();
  } else if (strcmp(cmd, "set") == 0 && arg) {
    int t = atoi(arg);
    if (t >= 0 && t <= 100) {
      threshold = t;
      saveSettings();
      Serial.print(F("threshold=")); Serial.println(threshold);
    } else {
      Serial.println(F("threshold must be 0-100"));
    }
  } else if (strcmp(cmd, "cal") == 0) {
    runCalibration();
  } else {
    Serial.println(F("unknown command, try 'help'"));
  }
}

void runCalibration() {
  Serial.println(F("--- calibration ---"));
  Serial.println(F("Step 1: insert sensor in DRY soil (or hold in air). Press Enter."));
  waitForEnter();
  uint16_t dry = readSensorRaw();
  Serial.print(F("dry raw=")); Serial.println(dry);

  Serial.println(F("Step 2: insert sensor in a cup of WATER. Press Enter."));
  waitForEnter();
  uint16_t wet = readSensorRaw();
  Serial.print(F("wet raw=")); Serial.println(wet);

  if (dry <= wet || dry - wet < 50) {
    Serial.println(F("ERROR: dry and wet readings too close. Check wiring/power."));
    beep(3, 200);
    return;
  }
  calDry = dry;
  calWet = wet;
  saveSettings();
  Serial.print(F("calibrated. dry=")); Serial.print(calDry);
  Serial.print(F(" wet=")); Serial.print(calWet);
  Serial.print(F(" threshold=")); Serial.println(threshold);
  beep(2, 150);
}

void waitForEnter() {
  Serial.flush();
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') return;
    }
    // keep button usable during calibration
    if (digitalRead(PIN_BUTTON) == LOW) return;
    delay(20);
  }
}

void pollSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (lineLen > 0) {
        line[lineLen] = '\0';
        processCommand(line);
        lineLen = 0;
      }
    } else if (lineLen < sizeof(line) - 1) {
      line[lineLen++] = c;
    }
  }
}

// ---------- Setup / loop ----------
void setup() {
  pinMode(PIN_SENSOR_POWER, OUTPUT);
  pinMode(PIN_PUMP, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  pumpOff();
  loadSettings();
  lastReadMs = millis();

  Serial.begin(9600);
  delay(50);
  Serial.println(F("Plant Watering System ready. Type 'help'."));
  if (!calibrated) {
    Serial.println(F("WARNING: not calibrated yet. Type 'cal'."));
    beep(3, 150);
  } else {
    Serial.print(F("calibrated dry=")); Serial.print(calDry);
    Serial.print(F(" wet=")); Serial.print(calWet);
    Serial.print(F(" threshold=")); Serial.println(threshold);
  }
}

void loop() {
  pollSerial();

  // Manual water button (debounced by state check + cooldown)
  if (digitalRead(PIN_BUTTON) == LOW && state != PUMPING) {
    Serial.println(F("button: watering"));
    pumpOn();
    state = PUMPING;
    lastChangeMs = millis();
  }

  switch (state) {
    case IDLE: {
      uint32_t now = millis();
      if (now - lastReadMs >= READ_INTERVAL_MS) {
        lastReadMs = now;
        uint16_t raw = readSensorRaw();
        int pct = moisturePercent(raw);

        if (raw >= 1010) {           // sensor disconnected or read open circuit
          Serial.println(F("SENSOR ERROR: raw near 1023, skipping this cycle"));
          return;
        }
        Serial.print(F("check: moisture=")); Serial.print(pct);
        Serial.println(F("%"));

        if (pct < threshold) {
          Serial.print(F("soil below ")); Serial.print(threshold);
          Serial.println(F("%, watering"));
          pumpOn();
          state = PUMPING;
          lastChangeMs = now;
        }
      }
      break;
    }

    case PUMPING: {
      uint32_t runMs = millis() - lastChangeMs;
      if (runMs >= PUMP_ON_MS) {
        pumpOff();
        state = COOLDOWN;
        lastChangeMs = millis();
        Serial.println(F("watering done, cooling down"));
      } else if (runMs >= MAX_PUMP_RUN_MS) {
        pumpOff();
        state = COOLDOWN;
        lastChangeMs = millis();
        Serial.println(F("SAFETY: max pump time hit"));
        beep(3, 300);
      }
      break;
    }

    case COOLDOWN: {
      if (millis() - lastChangeMs >= COOLDOWN_MS) {
        state = IDLE;
        lastReadMs = millis();
        Serial.println(F("cooldown over"));
      }
      break;
    }
  }
}
