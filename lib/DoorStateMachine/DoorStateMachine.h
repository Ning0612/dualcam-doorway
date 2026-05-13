#pragma once
#include <Arduino.h>
#include "states.h"

class DoorStateMachine {
public:
  DoorStateMachine();

  SystemState getState() const { return _state; }

  void onIndoorFaceDetected();
  void onOutdoorFaceDetected();
  void onDoorOpened();
  void onDoorClosed();
  void onUnknownVisitor();
  void onAlert();

  // Call every loop() iteration to handle state timeouts
  void tick();

private:
  SystemState   _state;
  unsigned long _stateEnteredAt;

  void _transitionTo(SystemState next);
};
