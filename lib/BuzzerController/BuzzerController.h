#pragma once
#include <Arduino.h>

// Simple buzzer on/off controller.
class BuzzerController {
public:
  static void begin(uint8_t pin);
  static void trigger();     // turn buzzer on
  static void cancel();      // turn buzzer off
  static bool isActive();
};
