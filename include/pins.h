#pragma once

// FaceGuard pin assignments (NMK99 ESP32-CAM / AI Thinker compatible)
//
// Camera occupies: 0(XCLK), 5(D0), 18(D1), 19(D2), 21(D3), 22(PCLK),
//                  23(HREF), 25(VSYNC), 26(SIOD/SDA), 27(SIOC/SCL),
//                  34(D6), 35(D7), 36(D4), 39(D5).
// GPIO 25/26/27 must NOT be used for any other output.
// GPIO 32 = CAM_PWDN on many boards; on NMK99 PWDN is not wired,
//           so GPIO 32 is available for WS2812B LED data.
//
// ADC1 (GPIO 32–39) works with WiFi active; ADC2 does not.
// GPIO 33 = ADC1_CH5 — safe for Hall sensor while WiFi is up.
//
// GPIO 34, 35, 36, 39 are INPUT-ONLY with NO internal pull-up.
// GPIO 0  = CAM_XCLK / boot-strap — do not use as general GPIO.
// GPIO 12 = boot-strap — do not use.

// WS2812B addressable RGB LED (single data line, GRB pixel order)
#define PIN_LED_DATA 32   // GPIO 32 — NMK99 onboard WS2812B; CAM_PWDN set to -1

// Actuators
#define PIN_BUZZER  13   // OUTPUT — piezo buzzer

// Sensors
#define PIN_HALL    33   // ADC INPUT — Hall-effect door sensor (ADC1_CH5)
