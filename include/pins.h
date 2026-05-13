#pragma once

// Shared pin assignments (both agents)
// NOTE: GPIO 32 is camera PWDN on many ESP32-CAM boards.
// For Phase 1 (no camera), this is safe. Revisit in Phase 4.
#define PIN_LED    33   // OUTPUT
#define PIN_BUZZER 32   // OUTPUT

// Indoor-only — GPIO 34 is input-only on ESP32 and does NOT support internal pull-up.
// Requires external 10kΩ pull-up to 3.3V. Door closed = HIGH, door open = LOW.
#define PIN_DOOR   34
