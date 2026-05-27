#pragma once
#include <Arduino.h>
#include "states.h"

// WS2812B (NeoPixel) RGB LED driver.
// Call tick() every loop() iteration to handle blinking.
//
// Display priority (highest wins):
//   isAlarmActive  → red blinking (250 ms)
//   ALERT_GREEN    → green solid (known-confirmed window)
//   face detected  → white solid (fill light for recognition)
//   idle           → off
//
// Use updateLed() in main loop to apply the above logic automatically.
// setWhite() / setOff() bypass the AlertLevel mapping for direct control.
class LedController {
public:
  // dataPin: WS2812B data line GPIO (e.g. PIN_LED_DATA).
  static void begin(uint8_t dataPin);

  // Set the base color from AlertLevel (does not change blinking state).
  static void setLevel(AlertLevel level);

  // Enable or disable blinking (250 ms period). Blinking color follows setLevel().
  static void setBlinking(bool enable);

  // White fill light (clears blinking).
  static void setWhite();

  // Turn LED off (clears blinking).
  static void setOff();

  // Call every loop() iteration to apply blinking.
  static void tick();
};
