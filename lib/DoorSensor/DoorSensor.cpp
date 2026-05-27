#include "DoorSensor.h"
#include "config.h"

// ── Static state ──────────────────────────────────────────────────────────────

static uint8_t      _pin            = 0;
static int          _lower          = HALL_DEFAULT_LOWER;
static int          _upper          = HALL_DEFAULT_UPPER;
static int          _hysteresis     = HALL_HYSTERESIS;
static DoorState    _state          = DoorState::DOOR_CLOSED;
static uint16_t     _raw            = 0;
static unsigned long _lastReadMs    = 0;

static bool          _pendingActive = false;
static bool          _pendingState  = false;   // true = pending CLOSED
static unsigned long _pendingMs     = 0;

static void (*_onChange)(DoorState) = nullptr;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Returns true if bounds are valid (sufficient gap for hysteresis zones).
static bool _boundsValid(int lo, int hi, int hys) {
  return (hi > lo) && (hi - lo > 2 * hys) && (hi <= 4095) && (lo >= 0);
}

// Evaluate raw against current bounds. Returns:
//   1  = definitely CLOSED  (outside open zone with hysteresis)
//  -1  = definitely OPEN    (inside open zone with hysteresis)
//   0  = dead zone          (ambiguous; keep current state)
static int _classify(int raw) {
  if (raw < _lower - _hysteresis || raw > _upper + _hysteresis) return  1;
  if (raw > _lower + _hysteresis && raw < _upper - _hysteresis) return -1;
  return 0;
}

// ── Public API ────────────────────────────────────────────────────────────────

void DoorSensor::begin(uint8_t pin, uint16_t lowerBound, uint16_t upperBound, uint16_t hysteresis) {
  _pin        = pin;
  _hysteresis = (int)((hysteresis > 2047) ? 2047 : hysteresis);

  if (_boundsValid((int)lowerBound, (int)upperBound, _hysteresis)) {
    _lower = (int)lowerBound;
    _upper = (int)upperBound;
  } else {
    Serial.printf("[DoorSensor] WARNING: invalid bounds (%u/%u), using defaults.\n",
                  lowerBound, upperBound);
    _lower = HALL_DEFAULT_LOWER;
    _upper = HALL_DEFAULT_UPPER;
  }

  // ADC_11db: full-scale 0–3.3 V maps to 0–4095
  analogSetPinAttenuation(_pin, ADC_11db);

  // Read initial state without debounce so setup() reflects reality immediately.
  // Dead zone at startup defaults to CLOSED (security-safe side).
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(_pin);
  _raw = (uint16_t)(sum >> 3);

  int c = _classify((int)_raw);
  _state = (c == -1) ? DoorState::DOOR_OPEN : DoorState::DOOR_CLOSED;
}

void DoorSensor::tick() {
  if (millis() - _lastReadMs < HALL_SAMPLE_INTERVAL_MS) return;
  _lastReadMs = millis();

  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(_pin);
  _raw = (uint16_t)(sum >> 3);

  int c = _classify((int)_raw);
  if (c == 0) {
    // Dead zone: clear pending and keep current state
    _pendingActive = false;
    return;
  }

  bool shouldBeClosed = (c == 1);
  bool isClosed       = (_state == DoorState::DOOR_CLOSED);

  if (shouldBeClosed == isClosed) {
    _pendingActive = false;
    return;
  }

  // Require stable new reading for DOOR_DEBOUNCE_MS
  if (!_pendingActive || _pendingState != shouldBeClosed) {
    _pendingActive = true;
    _pendingState  = shouldBeClosed;
    _pendingMs     = millis();
    return;
  }

  if (millis() - _pendingMs < DOOR_DEBOUNCE_MS) return;

  // Commit transition
  _pendingActive = false;
  _state = shouldBeClosed ? DoorState::DOOR_CLOSED : DoorState::DOOR_OPEN;

  if (_onChange) _onChange(_state);
}

DoorState DoorSensor::getState() { return _state; }
uint16_t  DoorSensor::getRaw()   { return _raw; }

void DoorSensor::setBounds(uint16_t lower, uint16_t upper) {
  int lo = (int)lower;
  int hi = (int)upper;
  if (!_boundsValid(lo, hi, _hysteresis)) return;

  _lower = lo;
  _upper = hi;
  _pendingActive = false;  // reset debounce so new bounds take effect immediately

  // Re-evaluate current state from most recent ADC reading.
  int c = _classify((int)_raw);
  if      (c ==  1) _state = DoorState::DOOR_CLOSED;
  else if (c == -1) _state = DoorState::DOOR_OPEN;
  // dead zone → leave _state unchanged until next stable reading
}

void DoorSensor::setOnChange(void (*cb)(DoorState)) { _onChange = cb; }
