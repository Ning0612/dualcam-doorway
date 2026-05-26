#pragma once
#include <Arduino.h>

// Passive buzzer driver using Arduino-ESP32 LEDC API.
// Channel 7 (HIGH_SPEED Timer 3); camera uses channel 0 (Timer 0) — no conflict.
// trigger() / cancel() control the alarm tone.
// testBeep() plays a non-blocking one-shot tone that expires in tick().
// setFrequency() takes effect immediately if the buzzer is active.
class BuzzerController {
public:
  static void     begin(uint8_t pin, uint32_t freqHz);
  static void     trigger();
  static void     cancel();
  static void     setFrequency(uint32_t freqHz);
  static uint32_t getFrequency();
  static void     testBeep(uint32_t freqHz, uint32_t durationMs);
  static bool     isActive();
  static void     tick();
};
