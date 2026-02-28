/*
 * SimRacingKit V3 - DIAGNOSTYCZNY
 * Dla Raspberry Pi Pico / RP2040-Zero
 *
 * Funkcje:
 * - 8 przycisków (GP0-GP7 - nowe mapowanie)
 * - Hamulec analogowy (GP26)
 * - DIAGNOSTYKA SERIAL: Otwórz Serial Monitor (115200), aby sprawdzić czy przyciski działają fizycznie!
 */

#include <Arduino.h>
#include <EEPROM.h>
#include "Adafruit_TinyUSB.h"

// --- KONFIGURACJA PINÓW ---
const int gearPins[] = {0, 1, 2, 6, 5, 7, 3, 4};
const int numGears = 8;
const int POT_PIN = 26;
const int LED_PIN = 13; // Standardowa dioda LED dla Pico (na Zero może nie być, ale nie zaszkodzi)

// --- STRUKTURA EEPROM ---
struct Config {
  uint16_t min_val;
  uint16_t max_val;
  uint8_t dz_start;
  uint8_t dz_end;
  uint32_t magic;
};
const uint32_t MAGIC_VAL = 0xABCD1234;
Config cfg;

// --- DESKRYPTOR HID V3 (Pancerny) ---
uint8_t const custom_hid_report[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)

    // 8 Przycisków (1 bajt)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x08,        //   Usage Maximum (Button 8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Var, Abs)

    // Oś Z (Hamulec) - 16-bit (2 bajty)
    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x32,        //   Usage (Z)
    0x16, 0x00, 0x80,  //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,  //   Logical Maximum (32767)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data, Var, Abs)

    // Padding (1 bajt dopełnienia dla wyrównania do 4 bajtów - poprawia kompatybilność)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x03,        //   Input (Constant, Var, Abs)

    0xC0               // End Collection
};

// Struktura raportu (Łącznie 4 bajty)
struct __attribute__((packed)) {
  uint8_t buttons = 0;
  int16_t axisZ = -32768;
  uint8_t padding = 0;
} hid_report;

bool firstRun = true;
Adafruit_USBD_HID usb_hid;

// --- LOGIKA SHIFTERA ---
bool lastButtonState[numGears];
unsigned long lastDebounceTime[numGears];
const unsigned long debounceDelay = 20;

String inputBuffer = "";

void loadConfig() {
  EEPROM.begin(256);
  EEPROM.get(0, cfg);
  if (cfg.magic != MAGIC_VAL) {
    cfg.min_val = 0; cfg.max_val = 65535; cfg.dz_start = 0; cfg.dz_end = 0; cfg.magic = MAGIC_VAL;
  }
}

int16_t processValue(uint16_t raw) {
  uint32_t min_v = cfg.min_val;
  uint32_t max_v = cfg.max_val;
  if (min_v == max_v) return -32768;
  uint32_t range = (max_v > min_v) ? (max_v - min_v) : (min_v - max_v);
  uint32_t start_v = min_v + (range * cfg.dz_start / 100);
  uint32_t end_v = max_v - (range * cfg.dz_end / 100);
  if (min_v < max_v) {
    if (raw <= start_v) return -32768; if (raw >= end_v) return 32767;
    return (int16_t)map(raw, start_v, end_v, -32768, 32767);
  } else {
    if (raw >= start_v) return -32768; if (raw <= end_v) return 32767;
    return (int16_t)map(raw, start_v, end_v, -32768, 32767);
  }
}

void setup() {
  Serial.begin(115200);
  // Poczekaj chwilę na Serial, ale nie blokuj
  unsigned long startWait = millis();
  while(!Serial && (millis() - startWait < 1000));

  Serial.println("--- SimRacingKit V3 START ---");

  analogReadResolution(16);
  pinMode(LED_PIN, OUTPUT);

  for (int i = 0; i < numGears; i++) {
    pinMode(gearPins[i], INPUT_PULLUP);
    lastButtonState[i] = false;
    lastDebounceTime[i] = 0;
  }

  loadConfig();
  usb_hid.setPollInterval(1);
  usb_hid.setReportDescriptor(custom_hid_report, sizeof(custom_hid_report));
  USBDevice.setProductDescriptor("SimRacingKit V3");
  USBDevice.setManufacturerDescriptor("SimRacingKit");
  usb_hid.begin();
}

void loop() {
  bool changed = false;

  // 1. OBSŁUGA SHIFTERA
  uint8_t currentButtons = 0;
  for (int i = 0; i < numGears; i++) {
    bool reading = !digitalRead(gearPins[i]);
    if (reading != lastButtonState[i]) {
      if ((millis() - lastDebounceTime[i]) > debounceDelay) {
        lastButtonState[i] = reading;
        lastDebounceTime[i] = millis();

        // --- DIAGNOSTYKA SERIAL ---
        Serial.print("Przycisk "); Serial.print(i + 1);
        Serial.println(reading ? ": WCISNIETY" : ": ZWOLNIONY");
        digitalWrite(LED_PIN, reading); // Dioda świeci jak przycisk wciśnięty
      }
    }
    if (lastButtonState[i]) currentButtons |= (1 << i);
  }

  if (currentButtons != hid_report.buttons) {
    hid_report.buttons = currentButtons;
    changed = true;
  }

  // 2. OBSŁUGA HAMULCA
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogRead(POT_PIN);
  uint16_t current_raw = sum / 16;
  int16_t z_val = processValue(current_raw);

  if (z_val != hid_report.axisZ) {
    hid_report.axisZ = z_val;
    changed = true;
  }

  // 3. WYSYŁANIE RAPORTU HID
  if ((changed || firstRun) && usb_hid.ready()) {
    usb_hid.sendReport(0, &hid_report, sizeof(hid_report));
    firstRun = false;
  }

  // 4. PROTOKÓŁ SERIAL (Kalibracja)
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      inputBuffer.trim();
      if (inputBuffer == "READ") {
        Serial.print("RAW:"); Serial.println(current_raw);
      } else if (inputBuffer == "GET_CONFIG") {
        Serial.printf("CONF:%u:%u:%u:%u\n", cfg.min_val, cfg.max_val, cfg.dz_start, cfg.dz_end);
      } else if (inputBuffer.startsWith("SET ")) {
        int v_min, v_max, dzs, dze;
        if (sscanf(inputBuffer.c_str(), "SET %d %d %d %d", &v_min, &v_max, &dzs, &dze) == 4) {
          cfg.min_val = v_min; cfg.max_val = v_max; cfg.dz_start = dzs; cfg.dz_end = dze;
          Serial.println("OK");
        }
      } else if (inputBuffer == "SAVE") {
        cfg.magic = MAGIC_VAL;
        EEPROM.put(0, cfg);
        EEPROM.commit();
        Serial.println("SAVED");
      } else if (inputBuffer == "PING") {
        Serial.println("PONG");
      }
      inputBuffer = "";
    } else {
      inputBuffer += c;
    }
  }

  delay(2); // Przyspieszamy pętlę dla lepszej diagnostyki
}
