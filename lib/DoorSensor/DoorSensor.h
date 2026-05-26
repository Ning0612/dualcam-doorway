#pragma once
#include <Arduino.h>
#include "states.h"

// Hall-effect door sensor driver.
//
// Reads 8 ADC samples and averages to reduce noise. Uses hysteresis to prevent
// rapid toggling near the threshold, and debouncing to require a stable new
// state before firing the onChange callback.
//
// Calibration: use Serial 'h' / 'H' commands in main.cpp, or call setThreshold().
class DoorSensor {
public:
  // pin:       analog input GPIO (ADC1 only; ADC2 does not work with WiFi).
  // threshold: ADC mid-point; below = OPEN, above = CLOSED.
  // hysteresis: dead-zone width on each side of threshold.
  static void begin(uint8_t pin, uint16_t threshold, uint16_t hysteresis);

  // Call every loop() iteration. Fires onChange callback on confirmed transitions.
  static void tick();

  static DoorState getState();
  static uint16_t  getRaw();

  // Update threshold at runtime (e.g., from settings save).
  static void setThreshold(uint16_t t);

  // Callback fired on every confirmed DoorState transition.
  static void setOnChange(void (*cb)(DoorState newState));
};
