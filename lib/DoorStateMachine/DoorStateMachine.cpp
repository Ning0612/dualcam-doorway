#include "DoorStateMachine.h"
#include "config.h"

DoorStateMachine::DoorStateMachine()
  : _state(SystemState::IDLE), _stateEnteredAt(0), _callback(nullptr) {}

void DoorStateMachine::_transitionTo(SystemState next) {
  SystemState from = _state;
  _state           = next;
  _stateEnteredAt  = millis();
  if (_callback) {
    StateEvent ev = { from, next, millis() };
    _callback(ev);
  }
}

void DoorStateMachine::onIndoorFaceDetected() {
  if (_state == SystemState::IDLE || _state == SystemState::HOME_OCCUPIED) {
    _transitionTo(SystemState::PREPARE_TO_LEAVE);
  }
}

void DoorStateMachine::onOutdoorFaceDetected() {
  if (_state == SystemState::IDLE || _state == SystemState::HOME_EMPTY) {
    _transitionTo(SystemState::PREPARE_TO_ENTER);
  }
}

void DoorStateMachine::onDoorOpened() {
  if (_state == SystemState::PREPARE_TO_LEAVE) {
    _transitionTo(SystemState::LEAVING_HOME);
  } else if (_state == SystemState::PREPARE_TO_ENTER) {
    _transitionTo(SystemState::ENTERING_HOME);
  }
}

void DoorStateMachine::onDoorClosed() {
  if (_state == SystemState::LEAVING_HOME) {
    _transitionTo(SystemState::HOME_EMPTY);
  } else if (_state == SystemState::ENTERING_HOME) {
    _transitionTo(SystemState::HOME_OCCUPIED);
  }
}

void DoorStateMachine::onUnknownVisitor() {
  if (_state == SystemState::PREPARE_TO_ENTER || _state == SystemState::IDLE) {
    _transitionTo(SystemState::UNKNOWN_VISITOR);
  }
}

void DoorStateMachine::onAlert() {
  if (_state == SystemState::UNKNOWN_VISITOR) {
    _transitionTo(SystemState::ALERT_MODE);
  }
}

void DoorStateMachine::tick() {
  unsigned long elapsed = millis() - _stateEnteredAt;

  switch (_state) {
    case SystemState::PREPARE_TO_LEAVE:
    case SystemState::PREPARE_TO_ENTER:
      if (elapsed >= FACE_RECENT_MS) _transitionTo(SystemState::IDLE);
      break;
    case SystemState::LEAVING_HOME:
      if (elapsed >= DOOR_TRANSITION_MS) _transitionTo(SystemState::HOME_EMPTY);
      break;
    case SystemState::ENTERING_HOME:
      if (elapsed >= DOOR_TRANSITION_MS) _transitionTo(SystemState::HOME_OCCUPIED);
      break;
    case SystemState::UNKNOWN_VISITOR:
    case SystemState::ALERT_MODE:
      if (elapsed >= UNKNOWN_VISITOR_MS) _transitionTo(SystemState::IDLE);
      break;
    default:
      break;
  }
}
