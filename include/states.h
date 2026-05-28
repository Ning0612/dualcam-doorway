#pragma once

// ── FaceGuard state enums ─────────────────────────────────────────────────────

enum class DoorState { DOOR_CLOSED, DOOR_OPEN };

enum class FaceState { FACE_NO_FACE, FACE_KNOWN, FACE_UNKNOWN };

// Temporal voting result from FaceVoter.
// NOTE: also defined here so SecurityStateMachine and LogManager can use it
// without pulling in FaceVoter.h.
enum class VoteResult { NONE, KNOWN_CONFIRMED, UNKNOWN_CONFIRMED };

enum class AlertLevel { ALERT_GREEN, ALERT_YELLOW, ALERT_RED };

enum class AlarmDecision { NO_ACTION, TRIGGER_ALARM, CANCEL_ALARM };

// Used by DiscordNotifier for per-event rate limiting (replaces old SystemState index).
enum class AlertEvent { UNKNOWN_VISITOR = 0, USER_RETURNED = 1, BOOT = 2 };

// ── String helpers ────────────────────────────────────────────────────────────

inline const char* alertLevelToString(AlertLevel l) {
  switch (l) {
    case AlertLevel::ALERT_GREEN:  return "ALERT_GREEN";
    case AlertLevel::ALERT_YELLOW: return "ALERT_YELLOW";
    case AlertLevel::ALERT_RED:    return "ALERT_RED";
    default:                       return "UNKNOWN";
  }
}

inline const char* doorStateToString(DoorState d) {
  return d == DoorState::DOOR_OPEN ? "DOOR_OPEN" : "DOOR_CLOSED";
}

inline const char* faceStateToString(FaceState f) {
  switch (f) {
    case FaceState::FACE_NO_FACE: return "FACE_NO_FACE";
    case FaceState::FACE_KNOWN:   return "FACE_KNOWN";
    case FaceState::FACE_UNKNOWN: return "FACE_UNKNOWN";
    default:                      return "UNKNOWN";
  }
}

inline const char* voteResultToString(VoteResult v) {
  switch (v) {
    case VoteResult::NONE:               return "NONE";
    case VoteResult::KNOWN_CONFIRMED:    return "KNOWN_CONFIRMED";
    case VoteResult::UNKNOWN_CONFIRMED:  return "UNKNOWN_CONFIRMED";
    default:                             return "UNKNOWN";
  }
}
