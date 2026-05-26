#include "BuzzerController.h"

static uint8_t _pin    = 0;
static bool    _active = false;

void BuzzerController::begin(uint8_t pin) {
  _pin = pin;
  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, LOW);
}

void BuzzerController::trigger() {
  _active = true;
  digitalWrite(_pin, HIGH);
}

void BuzzerController::cancel() {
  _active = false;
  digitalWrite(_pin, LOW);
}

bool BuzzerController::isActive() { return _active; }
