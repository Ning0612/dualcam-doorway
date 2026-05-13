#pragma once

enum class SystemState {
  IDLE,
  PREPARE_TO_LEAVE,
  PREPARE_TO_ENTER,
  LEAVING_HOME,
  ENTERING_HOME,
  HOME_OCCUPIED,
  HOME_EMPTY,
  UNKNOWN_VISITOR,
  ALERT_MODE
};

inline const char* stateToString(SystemState s) {
  switch (s) {
    case SystemState::IDLE:             return "IDLE";
    case SystemState::PREPARE_TO_LEAVE: return "PREPARE_TO_LEAVE";
    case SystemState::PREPARE_TO_ENTER: return "PREPARE_TO_ENTER";
    case SystemState::LEAVING_HOME:     return "LEAVING_HOME";
    case SystemState::ENTERING_HOME:    return "ENTERING_HOME";
    case SystemState::HOME_OCCUPIED:    return "HOME_OCCUPIED";
    case SystemState::HOME_EMPTY:       return "HOME_EMPTY";
    case SystemState::UNKNOWN_VISITOR:  return "UNKNOWN_VISITOR";
    case SystemState::ALERT_MODE:       return "ALERT_MODE";
    default:                            return "UNKNOWN";
  }
}
