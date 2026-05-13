#include <Arduino.h>
#include "config.h"
#include "pins.h"
#include "states.h"
#include "DoorStateMachine.h"

DoorStateMachine sm;

static bool          doorStable     = false;  // last debounced door state (true = open)
static bool          doorRaw        = false;  // last raw reading
static unsigned long doorChangeMs   = 0;

static void logStateChange(SystemState s) {
  Serial.print("[Indoor] state -> ");
  Serial.println(stateToString(s));
}

static void handleSerialInput() {
  if (!Serial.available()) return;
  char c = Serial.read();

  SystemState prev = sm.getState();
  if (c == 'f') {
    Serial.println("[Indoor] face detected (simulated)");
    sm.onIndoorFaceDetected();
  } else if (c == 'u') {
    Serial.println("[Indoor] unknown visitor (simulated)");
    sm.onUnknownVisitor();
  } else if (c == 'a') {
    Serial.println("[Indoor] alert triggered (simulated)");
    sm.onAlert();
  }
  if (sm.getState() != prev) logStateChange(sm.getState());
}

static void handleDoorSensor() {
  bool current = (digitalRead(PIN_DOOR) == LOW);  // active LOW: closed = HIGH via pull-up
  unsigned long now = millis();

  if (current != doorRaw) {
    doorRaw      = current;
    doorChangeMs = now;
  }

  if ((now - doorChangeMs >= DOOR_DEBOUNCE_MS) && (doorRaw != doorStable)) {
    doorStable = doorRaw;
    SystemState prev = sm.getState();

    if (doorStable) {
      Serial.println("[Indoor] door opened");
      sm.onDoorOpened();
    } else {
      Serial.println("[Indoor] door closed");
      sm.onDoorClosed();
    }

    if (sm.getState() != prev) logStateChange(sm.getState());
  }
}

static void updateActuators() {
  SystemState s = sm.getState();
  // LED on during active states; off at IDLE / stable home states
  bool ledOn = (s == SystemState::PREPARE_TO_LEAVE  ||
                s == SystemState::PREPARE_TO_ENTER  ||
                s == SystemState::LEAVING_HOME       ||
                s == SystemState::ENTERING_HOME      ||
                s == SystemState::UNKNOWN_VISITOR    ||
                s == SystemState::ALERT_MODE);
  digitalWrite(PIN_LED, ledOn ? HIGH : LOW);

  // Buzzer on ALERT_MODE only
  digitalWrite(PIN_BUZZER, (s == SystemState::ALERT_MODE) ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);
  Serial.println("[Indoor] boot");

  pinMode(PIN_LED,    OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  // GPIO 34 is input-only with no internal pull-up; external 10kΩ pull-up to 3.3V required.
  pinMode(PIN_DOOR,   INPUT);

  digitalWrite(PIN_LED,    LOW);
  digitalWrite(PIN_BUZZER, LOW);

  // Sync initial door state to avoid spurious event on first loop iteration
  doorRaw    = (digitalRead(PIN_DOOR) == LOW);
  doorStable = doorRaw;

  Serial.println("[Indoor] ready — keys: f=face, u=unknown, a=alert");
}

void loop() {
  handleSerialInput();
  handleDoorSensor();

  SystemState prev = sm.getState();
  sm.tick();
  if (sm.getState() != prev) logStateChange(sm.getState());

  updateActuators();
}
