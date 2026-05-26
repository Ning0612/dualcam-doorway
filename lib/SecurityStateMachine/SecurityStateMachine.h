#pragma once
#include <Arduino.h>
#include "states.h"

// Security state machine for Agent 1.
//
// Combines DoorState, FaceVoter results, and Agent 2 presence into a single
// AlertLevel that drives LedController and BuzzerController.
//
// AlertLevel derivation:
//   !agent2Online || !occupied → ALERT_RED   (independent operation)
//   occupied && agent2Online  → ALERT_YELLOW (coordinate with Agent 2)
//   KNOWN_CONFIRMED, no alarm → ALERT_GREEN  (brief; reverts to base on idle)
//
// Unknown visitor handling:
//   ALERT_RED  → immediate buzzer + LED blink + Discord + log
//   ALERT_YELLOW → notify Agent 2 via callback; wait for AlarmDecision
//                  (timeout ALARM_DECISION_TIMEOUT_MS → default TRIGGER_ALARM)

static constexpr unsigned long ALARM_DECISION_TIMEOUT_MS = 30000UL;
static constexpr unsigned long KNOWN_GREEN_DURATION_MS   = 15000UL;

class SecurityStateMachine {
public:
  // ── Event inputs (call from main.cpp callbacks) ───────────────────────────
  void onVoteResult(VoteResult result, const char* name = nullptr, float similarity = 0.0f);
  void onDoorChange(DoorState state);
  void onPresence(bool occupied);
  void onAlarmDecision(AlarmDecision d);
  void onAgent2Online(bool online);

  // Call every loop() iteration for timeout handling.
  void tick();

  // ── State queries ─────────────────────────────────────────────────────────
  AlertLevel  getAlertLevel()    const { return _alertLevel; }
  DoorState   getDoorState()     const { return _doorState; }
  FaceState   getFaceState()     const { return _faceState; }
  bool        isAgent2Online()   const { return _agent2Online; }
  bool        isAlarmActive()    const { return _alarmActive; }
  const char* getLastKnownUser() const { return _lastKnownUser; }

  // ── Callbacks (set before first call to any on*) ──────────────────────────

  // Fired when an unknown visitor triggers an alarm.
  // level: current alert level. eventType: "UNKNOWN_CONFIRMED".
  using AlertCallback = void(*)(AlertLevel level, const char* eventType);
  void setOnAlert(AlertCallback cb) { _onAlert = cb; }

  // Fired on confirmed DoorState transition, after logDoor/AgentComm updates.
  // relatedUser: last known user name if KNOWN_CONFIRMED preceded door open, else "".
  using DoorCallback = void(*)(DoorState state, const char* relatedUser);
  void setOnDoorEvent(DoorCallback cb) { _onDoorEvent = cb; }

  // Fired when KNOWN_CONFIRMED; caller should publish face event and update log.
  using KnownCallback = void(*)(const char* name, float similarity);
  void setOnKnownConfirmed(KnownCallback cb) { _onKnownConfirmed = cb; }

  // Fired when alarm is cancelled by Agent 2.
  using CancelCallback = void(*)();
  void setOnAlarmCancelled(CancelCallback cb) { _onAlarmCancelled = cb; }

private:
  AlertLevel  _alertLevel    = AlertLevel::ALERT_RED;
  DoorState   _doorState     = DoorState::DOOR_CLOSED;
  FaceState   _faceState     = FaceState::FACE_NO_FACE;
  bool        _agent2Online  = false;
  bool        _occupied      = false;
  bool        _alarmActive   = false;

  // Known-user green window
  bool          _knownConfirmed  = false;
  unsigned long _knownConfirmedMs = 0;
  char          _lastKnownUser[17] = {};
  float         _lastSimilarity   = 0.0f;

  // Yellow-alert decision window
  bool          _waitingForDecision = false;
  unsigned long _decisionStartMs    = 0;

  // Pending door-related user attribution
  char _pendingDoorUser[17] = {};

  AlertCallback  _onAlert          = nullptr;
  DoorCallback   _onDoorEvent      = nullptr;
  KnownCallback  _onKnownConfirmed = nullptr;
  CancelCallback _onAlarmCancelled = nullptr;

  void _recalcAlertLevel();
  void _triggerAlarm();
  void _cancelAlarm();
};
