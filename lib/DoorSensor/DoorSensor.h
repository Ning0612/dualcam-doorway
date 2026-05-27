#pragma once
#include <Arduino.h>
#include "states.h"

// Hall-effect door sensor driver.
//
// Dual-bound detection: door is OPEN when ADC reading is between lowerBound and
// upperBound (no magnetic field). Door is CLOSED when reading falls outside either
// bound (magnetic field detected, regardless of polarity).
//
// Hysteresis prevents chatter near the boundary edges. Dead zone: readings
// within one hysteresis width of a bound keep the current state.
//
// Calibrate: press serial 'H' while the door is CLOSED with the magnet engaged.
// The driver auto-detects deflection direction and offsets the bound accordingly.
class DoorSensor {
public:
  // lowerBound: lower edge of the open zone (raw below this = CLOSED).
  // upperBound: upper edge of the open zone (raw above this = CLOSED).
  // Requires: upperBound - lowerBound > 2 * hysteresis. Falls back to defaults if invalid.
  static void begin(uint8_t pin, uint16_t lowerBound, uint16_t upperBound, uint16_t hysteresis);

  // Call every loop() iteration. Fires onChange callback on confirmed transitions.
  static void tick();

  static DoorState getState();
  static uint16_t  getRaw();

  // Update bounds at runtime (e.g., from settings save or serial calibration).
  // No-op if bounds are invalid (lower >= upper or gap <= 2 * hysteresis).
  static void setBounds(uint16_t lower, uint16_t upper);

  // Callback fired on every confirmed DoorState transition.
  static void setOnChange(void (*cb)(DoorState newState));
};
