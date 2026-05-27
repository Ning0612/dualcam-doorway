#include "SecurityStateMachine.h"
#include <Arduino.h>

// ── Internal helpers ──────────────────────────────────────────────────────────

void SecurityStateMachine::_recalcAlertLevel() {
  // Active alarm pins level to RED; presence/connectivity changes cannot downgrade it.
  if (_alarmActive) {
    _alertLevel = AlertLevel::ALERT_RED;
    return;
  }
  // Known-user green window: brief ALERT_GREEN while known user is at door
  if (_knownConfirmed && (millis() - _knownConfirmedMs) < KNOWN_GREEN_DURATION_MS) {
    _alertLevel = AlertLevel::ALERT_GREEN;
    return;
  }
  // Base level from presence + Agent 2 connectivity
  _alertLevel = (_agent2Online && _occupied) ? AlertLevel::ALERT_YELLOW
                                             : AlertLevel::ALERT_RED;
}

void SecurityStateMachine::_silenceBuzzer() {
  if (!_buzzerActive) return;
  _buzzerActive = false;
  if (_onBuzzerSilence) _onBuzzerSilence();
}

void SecurityStateMachine::_triggerAlarm() {
  if (_alarmActive) return;
  _alarmActive        = true;
  _buzzerActive       = true;
  _buzzerStartMs      = millis();
  _waitingForDecision = false;
  _recalcAlertLevel();  // _alarmActive=true → always RED
  if (_onAlert) _onAlert(_alertLevel, "UNKNOWN_CONFIRMED");
}

void SecurityStateMachine::_cancelAlarm() {
  if (!_alarmActive) return;
  _alarmActive    = false;
  _knownConfirmed = false;
  _faceState      = FaceState::FACE_NO_FACE;  // clear stale FACE_KNOWN left by Condition C
  _silenceBuzzer();
  _recalcAlertLevel();
  if (_onAlarmCancelled) _onAlarmCancelled();
}

// ── Event inputs ──────────────────────────────────────────────────────────────

void SecurityStateMachine::onVoteResult(VoteResult result, const char* name, float similarity) {
  if (result == VoteResult::KNOWN_CONFIRMED) {
    _faceState        = FaceState::FACE_KNOWN;
    _knownConfirmed   = true;
    _knownConfirmedMs = millis();
    _lastSimilarity   = similarity;
    strncpy(_lastKnownUser, name ? name : "", sizeof(_lastKnownUser) - 1);
    _lastKnownUser[sizeof(_lastKnownUser) - 1] = '\0';

    // Door attribution: next door-open within KNOWN_GREEN_DURATION_MS uses this name
    strncpy(_pendingDoorUser, _lastKnownUser, sizeof(_pendingDoorUser) - 1);

    // Clear pending decision wait — known user resolves the uncertainty.
    _waitingForDecision = false;

    if (_onKnownConfirmed) _onKnownConfirmed(_lastKnownUser, _lastSimilarity);

    // Condition C: known user confirmed while alarm is active → cancel alarm.
    if (_alarmActive) _cancelAlarm();

    _recalcAlertLevel();

  } else if (result == VoteResult::UNKNOWN_CONFIRMED) {
    _faceState = FaceState::FACE_UNKNOWN;
    _recalcAlertLevel();

    if (_alertLevel == AlertLevel::ALERT_RED) {
      // Independent operation: trigger immediately
      _triggerAlarm();
    } else {
      // ALERT_YELLOW: request Agent 2 decision
      if (!_alarmActive && !_waitingForDecision) {
        _waitingForDecision = true;
        _decisionStartMs    = millis();
        if (_onAlert) _onAlert(_alertLevel, "UNKNOWN_CONFIRMED");
      }
    }
  }
}

void SecurityStateMachine::onDoorChange(DoorState state) {
  _doorState = state;

  const char* user = "";
  if (state == DoorState::DOOR_OPEN && _pendingDoorUser[0] != '\0') {
    // Only attribute door open to known user if within the confirmed time window
    if (millis() - _knownConfirmedMs < KNOWN_GREEN_DURATION_MS) {
      user = _pendingDoorUser;
    }
  }

  if (_onDoorEvent) _onDoorEvent(state, user);

  if (state == DoorState::DOOR_CLOSED) {
    memset(_pendingDoorUser, 0, sizeof(_pendingDoorUser));
    // Condition B: door closed while alarm is active → cancel alarm.
    if (_alarmActive) _cancelAlarm();
  }
}

void SecurityStateMachine::onPresence(bool occupied) {
  _occupied = occupied;
  _recalcAlertLevel();
}

void SecurityStateMachine::onAlarmDecision(AlarmDecision d) {
  if (d == AlarmDecision::TRIGGER_ALARM) {
    // Only accept TRIGGER if we actually requested a decision; prevents
    // replayed or retained MQTT messages from triggering alarm unexpectedly.
    if (_waitingForDecision) {
      _waitingForDecision = false;
      _triggerAlarm();
    }
  } else if (d == AlarmDecision::CANCEL_ALARM) {
    _waitingForDecision = false;
    _cancelAlarm();  // no-op if alarm not active (guards internally)
  }
  // NO_ACTION: leave current state
}

void SecurityStateMachine::onAgent2Online(bool online) {
  if (_agent2Online == online) return;
  _agent2Online = online;
  // Recalculate level first so _triggerAlarm() fires with the correct (RED) level
  _recalcAlertLevel();
  if (!online && _waitingForDecision) {
    // Agent 2 went offline while we were waiting for its decision → default trigger
    _waitingForDecision = false;
    _triggerAlarm();
  }
}

// ── Tick (timeout handling) ───────────────────────────────────────────────────

void SecurityStateMachine::tick() {
  unsigned long now = millis();

  // Expire known-user green window
  if (_knownConfirmed && (now - _knownConfirmedMs) >= KNOWN_GREEN_DURATION_MS) {
    _knownConfirmed = false;
    _faceState      = FaceState::FACE_NO_FACE;
    _recalcAlertLevel();
  }

  // Condition A: alarm auto-cancel after buzzer duration expires.
  if (_alarmActive && _buzzerActive && (now - _buzzerStartMs) >= _buzzerDurationMs) {
    _cancelAlarm();
  }

  // Yellow-alert decision timeout → default to TRIGGER_ALARM
  if (_waitingForDecision && (now - _decisionStartMs) >= ALARM_DECISION_TIMEOUT_MS) {
    _waitingForDecision = false;
    _triggerAlarm();  // internally forces RED via _recalcAlertLevel()
  }
}
