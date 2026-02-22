/*
 * SimRacingKit - H-Shifter for Raspberry Pi Pico (RP2040-Zero)
 *
 * Wykorzystuje rdzeń Earle Philhowera dla RP2040.
 * Wymaga wybrania w menu Tools -> USB Stack: "Adafruit TinyUSB"
 * (jeśli chcesz niestandardową nazwę urządzenia).
 */

#include "Adafruit_TinyUSB.h"

// Biblioteka Joystick dla rdzenia Philhowera
#include <Joystick.h>

// Definicja pinów dla biegów (zgodnie z zaproponowanym schematem)
// GP0 -> Bieg 1, GP1 -> Bieg 2, ..., GP6 -> Bieg 7, GP7 -> R
const int gearPins[] = {0, 1, 2, 3, 4, 5, 6, 7};
const int numGears = 8;

// Tablica do debouncingu
bool lastButtonState[numGears];
unsigned long lastDebounceTime[numGears];
const unsigned long debounceDelay = 20; // 20ms dla stabilności mechanicznych przełączników

void setup() {
  // Konfiguracja pinów
  for (int i = 0; i < numGears; i++) {
    pinMode(gearPins[i], INPUT_PULLUP);
    lastButtonState[i] = false;
    lastDebounceTime[i] = 0;
  }

  // Konfiguracja nazwy urządzenia (wymaga stosu Adafruit TinyUSB w menu IDE)
  // Jeśli używasz standardowego stosu Pico SDK, nazwa będzie domyślna.
  USBDevice.setProductDescriptor("SimRacingKit");
  USBDevice.setManufacturerDescriptor("SimRacingKit");

  // Inicjalizacja Joysticka
  Joystick.begin();
}

void loop() {
  for (int i = 0; i < numGears; i++) {
    // Odczyt fizyczny (LOW = wciśnięty przy INPUT_PULLUP)
    bool reading = !digitalRead(gearPins[i]);

    // Jeśli stan się zmienił (np. przez drgania styków)
    if (reading != lastButtonState[i]) {
      // Jeśli minęło wystarczająco dużo czasu od ostatniej zmiany
      if ((millis() - lastDebounceTime[i]) > debounceDelay) {
        lastButtonState[i] = reading;

        // Aktualizacja stanu przycisku w Joysticku (Philhower Core API: .button(index, state))
        // i to numer przycisku (0-7), reading to stan (true/false)
        Joystick.button(i + 1, reading); // Wiele gier woli numerację od 1, ale Philhower API używa 1-n

        lastDebounceTime[i] = millis();
      }
    }
  }
}
