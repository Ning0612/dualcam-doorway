#include "DoorSensor.h"
#include "config.h"

// ── Static state ──────────────────────────────────────────────────────────────

static uint8_t      _pin            = 0;
static uint16_t     _threshold      = HALL_DEFAULT_THRESHOLD;
static uint16_t     _hysteresis     = HALL_HYSTERESIS;
static DoorState    _state          = DoorState::DOOR_CLOSED;
static uint16_t     _raw            = 0;
static unsigned long _lastReadMs    = 0;

static bool          _pendingActive = false;
static bool          _pendingState  = false;
static unsigned long _pendingMs     = 0;

static void (*_onChange)(DoorState) = nullptr;

// ── Public API ────────────────────────────────────────────────────────────────

void DoorSensor::begin(uint8_t pin, uint16_t threshold, uint16_t hysteresis) {
  _pin = pin;
  // Cap hysteresis to half ADC range so lo/hi bands never cross or underflow
  _hysteresis = (hysteresis > 2047) ? 2047 : hysteresis;

  // Clamp threshold to a safe range where both hysteresis bands fit within [0, 4095]
  uint16_t lo = _hysteresis + 1;
  uint16_t hi = (uint16_t)(4095 - _hysteresis);
  _threshold  = (threshold < lo) ? lo : (threshold > hi ? hi : threshold);

  // ADC_11db: full-scale 0–3.3 V maps to 0–4095
  analogSetPinAttenuation(_pin, ADC_11db);

  // Read initial state without debounce so setup() reflects reality immediately
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(_pin);
  _raw   = (uint16_t)(sum >> 3);
  _state = (_raw < _threshold) ? DoorState::DOOR_OPEN : DoorState::DOOR_CLOSED;
}

void DoorSensor::tick() {
  if (millis() - _lastReadMs < HALL_SAMPLE_INTERVAL_MS) return;
  _lastReadMs = millis();

  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(_pin);
  _raw = (uint16_t)(sum >> 3);

  bool shouldBeOpen;
  if      (_raw < (uint16_t)(_threshold - _hysteresis)) shouldBeOpen = true;
  else if (_raw > (uint16_t)(_threshold + _hysteresis)) shouldBeOpen = false;
  else {
    // Dead zone: clear pending and keep current state
    _pendingActive = false;
    return;
  }

  if (shouldBeOpen == (_state == DoorState::DOOR_OPEN)) {
    _pendingActive = false;
    return;
  }

  // Require stable new reading for DOOR_DEBOUNCE_MS
  if (!_pendingActive || _pendingState != shouldBeOpen) {
    _pendingActive = true;
    _pendingState  = shouldBeOpen;
    _pendingMs     = millis();
    return;
  }

  if (millis() - _pendingMs < DOOR_DEBOUNCE_MS) return;

  // Commit transition
  _pendingActive = false;
  _state = shouldBeOpen ? DoorState::DOOR_OPEN : DoorState::DOOR_CLOSED;

  if (_onChange) _onChange(_state);
}

DoorState DoorSensor::getState() { return _state; }
uint16_t  DoorSensor::getRaw()   { return _raw; }

void DoorSensor::setThreshold(uint16_t t) {
  // Clamp to valid range (same logic as begin)
  uint16_t lo = _hysteresis + 1;
  uint16_t hi = (uint16_t)(4095 - _hysteresis);
  _threshold = (t < lo) ? lo : (t > hi ? hi : t);

  _pendingActive = false;  // reset debounce so new threshold takes effect immediately

  // Re-evaluate current state from the most recent ADC reading;
  // outside the dead zone → update _state immediately (no callback, no transition log)
  if      (_raw < (uint16_t)(_threshold - _hysteresis)) _state = DoorState::DOOR_OPEN;
  else if (_raw > (uint16_t)(_threshold + _hysteresis)) _state = DoorState::DOOR_CLOSED;
  // else: inside dead zone — leave _state unchanged until next stable reading
}

void DoorSensor::setOnChange(void (*cb)(DoorState)) { _onChange = cb; }
