#pragma once
#include <Arduino.h>
#include "states.h"

// WS2812B (NeoPixel) RGB LED driver driven by AlertLevel.
// Uses Adafruit NeoPixel library; pixel order must be NEO_GRB (standard WS2812B).
// Call tick() every loop() iteration to handle blinking.
//
// Color mapping:
//   ALERT_GREEN  → green solid
//   ALERT_YELLOW → yellow solid
//   ALERT_RED    → red solid (blinking when alarm active)
class LedController {
public:
  // dataPin: WS2812B data line GPIO (e.g. PIN_LED_DATA).
  static void begin(uint8_t dataPin);

  // Set the base color from AlertLevel (does not change blinking state).
  static void setLevel(AlertLevel level);

  // Enable or disable blinking (250 ms period). Blinking color follows setLevel().
  static void setBlinking(bool enable);

  // Call every loop() iteration to apply blinking.
  static void tick();
};
