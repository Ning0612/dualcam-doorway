#include "LedController.h"
#include <Adafruit_NeoPixel.h>

static Adafruit_NeoPixel* _strip      = nullptr;
static AlertLevel          _level     = AlertLevel::ALERT_RED;
static bool                _blinking  = false;
static bool                _ledOn     = true;
static unsigned long       _lastBlinkMs = 0;

static constexpr uint16_t BLINK_PERIOD_MS = 250;
static constexpr uint8_t  BRIGHTNESS      = 80;   // ~31% of full; adjust to taste

static void _applyColor(bool on) {
  if (!_strip) return;
  uint32_t c = 0;
  if (on) {
    switch (_level) {
      case AlertLevel::ALERT_GREEN:  c = _strip->Color(  0, 255,   0); break;
      case AlertLevel::ALERT_YELLOW: c = _strip->Color(255, 200,   0); break;
      case AlertLevel::ALERT_RED:
      default:                       c = _strip->Color(255,   0,   0); break;
    }
  }
  _strip->setPixelColor(0, c);
  _strip->show();
}

void LedController::begin(uint8_t dataPin) {
  // Static storage: constructor captures dataPin; begin() called only once.
  static Adafruit_NeoPixel instance(1, dataPin, NEO_GRB + NEO_KHZ800);
  _strip = &instance;
  _strip->begin();
  _strip->setBrightness(BRIGHTNESS);
  _strip->clear();
  _strip->show();  // boot: off (updateLed() will set correct state on first loop)
}

void LedController::setLevel(AlertLevel level) {
  _level = level;
  if (!_blinking) _applyColor(true);
}

void LedController::setBlinking(bool enable) {
  _blinking = enable;
  if (!enable) {
    _ledOn = true;
    _applyColor(true);
  }
}

void LedController::setWhite() {
  if (!_strip) return;
  _blinking = false;
  _ledOn    = true;
  _strip->setPixelColor(0, _strip->Color(255, 255, 255));
  _strip->show();
}

void LedController::setOff() {
  if (!_strip) return;
  _blinking = false;
  _ledOn    = true;
  _strip->setPixelColor(0, 0);
  _strip->show();
}

void LedController::tick() {
  if (!_blinking) return;
  unsigned long now = millis();
  if (now - _lastBlinkMs >= BLINK_PERIOD_MS) {
    _lastBlinkMs = now;
    _ledOn = !_ledOn;
    _applyColor(_ledOn);
  }
}
