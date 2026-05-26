#include "BuzzerController.h"

// Arduino-ESP32 2.x LEDC API. Channel 7 uses HIGH_SPEED Timer 3;
// camera uses channel 0 (HIGH_SPEED Timer 0) — no timer or channel conflict.
static const uint8_t  BUZZER_CH   = 7;
static const uint8_t  BUZZER_BITS = 8;
static const uint16_t BUZZER_DUTY = 128;  // 50% duty → square wave

static uint8_t       _pin         = 0;
static uint32_t      _freq        = 2000;
static bool          _active      = false;
static bool          _testing     = false;
static unsigned long _testStartMs = 0;
static unsigned long _testDurMs   = 0;

void BuzzerController::begin(uint8_t pin, uint32_t freqHz) {
  _pin  = pin;
  _freq = freqHz;
  ledcSetup(BUZZER_CH, _freq, BUZZER_BITS);
  ledcAttachPin(_pin, BUZZER_CH);
  ledcWrite(BUZZER_CH, 0);
}

void BuzzerController::trigger() {
  _active  = true;
  _testing = false;   // alarm overrides any ongoing test
  ledcSetup(BUZZER_CH, _freq, BUZZER_BITS);
  ledcWrite(BUZZER_CH, BUZZER_DUTY);
}

void BuzzerController::cancel() {
  _active  = false;
  _testing = false;
  ledcWrite(BUZZER_CH, 0);
}

void BuzzerController::setFrequency(uint32_t freqHz) {
  _freq = freqHz;
  if (_active) {
    ledcSetup(BUZZER_CH, _freq, BUZZER_BITS);
    ledcWrite(BUZZER_CH, BUZZER_DUTY);
  }
}

uint32_t BuzzerController::getFrequency() { return _freq; }

void BuzzerController::testBeep(uint32_t freqHz, uint32_t durationMs) {
  if (_active) return;
  _testing     = true;
  _testStartMs = millis();
  _testDurMs   = durationMs;
  ledcSetup(BUZZER_CH, freqHz, BUZZER_BITS);
  ledcWrite(BUZZER_CH, BUZZER_DUTY);
}

void BuzzerController::tick() {
  if (_testing && (millis() - _testStartMs) >= _testDurMs) {
    _testing = false;
    if (!_active) ledcWrite(BUZZER_CH, 0);
  }
}

bool BuzzerController::isActive() { return _active; }
