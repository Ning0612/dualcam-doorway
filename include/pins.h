#pragma once

// Shared pin assignments
// GPIO 32 is camera PWDN — not available for general use.
#define PIN_BUZZER  13  // OUTPUT

// GPIO 0 is CAM_XCLK on NMK99 — cannot be used as a general-purpose button.
// WiFi reset is handled via Serial 'W' command instead of a hardware button.

#ifdef INDOOR_AGENT
  // GPIO 33 (ADC1_CH5) is used for the Hall-effect door sensor.
  // ADC1 works with WiFi active; ADC2 does not.
  // GPIO 33 freed by moving LED to GPIO 4 (repurposed from flash LED).
  #define PIN_LED   4
  #define PIN_HALL  33  // analog INPUT, ADC1_CHANNEL_5
#else
  #define PIN_LED   33  // OUTPUT
#endif
