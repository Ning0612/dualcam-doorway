#include <Arduino.h>
#include "config.h"
#include "pins.h"
#include "states.h"
#include "DoorStateMachine.h"

DoorStateMachine sm;

static unsigned long lastBlinkMs = 0;
static bool          ledState    = false;

static void logStateChange(SystemState s) {
  Serial.print("[Outdoor] state -> ");
  Serial.println(stateToString(s));
}

static void handleSerialInput() {
  if (!Serial.available()) return;
  char c = Serial.read();

  SystemState prev = sm.getState();
  if (c == 'f') {
    Serial.println("[Outdoor] face detected (simulated)");
    sm.onOutdoorFaceDetected();
  } else if (c == 'u') {
    Serial.println("[Outdoor] unknown visitor (simulated)");
    sm.onUnknownVisitor();
  } else if (c == 'a') {
    Serial.println("[Outdoor] alert triggered (simulated)");
    sm.onAlert();
  }
  if (sm.getState() != prev) logStateChange(sm.getState());
}

static void updateActuators() {
  unsigned long now = millis();
  SystemState   s   = sm.getState();

  if (s == SystemState::ALERT_MODE || s == SystemState::UNKNOWN_VISITOR) {
    // Rapid blink on alert
    if (now - lastBlinkMs >= 250) {
      lastBlinkMs = now;
      ledState    = !ledState;
      digitalWrite(PIN_LED, ledState ? HIGH : LOW);
    }
    digitalWrite(PIN_BUZZER, (s == SystemState::ALERT_MODE) ? HIGH : LOW);
  } else {
    // Heartbeat: slow blink every 5s at IDLE, solid on at active states
    bool ledOn = (s != SystemState::IDLE);
    if (!ledOn) {
      if (now - lastBlinkMs >= 5000) {
        lastBlinkMs = now;
        ledState    = !ledState;
        digitalWrite(PIN_LED, ledState ? HIGH : LOW);
      }
    } else {
      digitalWrite(PIN_LED, HIGH);
    }
    digitalWrite(PIN_BUZZER, LOW);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("[Outdoor] boot");

  pinMode(PIN_LED,    OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_LED,    LOW);
  digitalWrite(PIN_BUZZER, LOW);

  Serial.println("[Outdoor] ready — keys: f=face, u=unknown, a=alert");
}

void loop() {
  handleSerialInput();

  SystemState prev = sm.getState();
  sm.tick();
  if (sm.getState() != prev) logStateChange(sm.getState());

  updateActuators();
}
