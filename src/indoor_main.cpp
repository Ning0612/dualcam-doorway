#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "config.h"
#include "pins.h"
#include "states.h"
#include "DoorStateMachine.h"
#include "ConfigPortal.h"
#include "SettingsStore.h"
#include "SessionAuth.h"
#include "AgentProtocol.h"
#include "DashboardServer.h"
#include "DiscordNotifier.h"

// ── Globals ───────────────────────────────────────────────────────────────────

WebServer        server(HTTP_PORT);
DoorStateMachine sm;
PeerStatus       cachedPeer = {false, SystemState::IDLE, 0};

static bool          doorOpen      = false;
static bool          doorStable    = false;
static bool          doorRaw       = false;
static unsigned long doorChangeMs  = 0;
static unsigned long lastPeerQuery = 0;

// ── State event callback ──────────────────────────────────────────────────────

static void onStateEvent(const StateEvent& ev) {
  Serial.print("[Indoor] state -> ");
  Serial.println(stateToString(ev.to));

  if (ev.to == SystemState::UNKNOWN_VISITOR || ev.to == SystemState::ALERT_MODE) {
    String url = SettingsStore::getDiscordUrl();
    if (url.length() > 0) {
      String msg = String("[DualCam Indoor] ") + stateToString(ev.to);
      DiscordNotifier::notify(url, ev.to, msg);
    }
  }
}

// ── Input handling ────────────────────────────────────────────────────────────

static void handleSerialInput() {
  if (!Serial.available()) return;
  char c = Serial.read();
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
}

static void handleDoorSensor() {
  bool         current = (digitalRead(PIN_DOOR) == LOW);  // active-LOW with external pull-up
  unsigned long now    = millis();

  if (current != doorRaw) {
    doorRaw      = current;
    doorChangeMs = now;
  }

  if ((now - doorChangeMs >= DOOR_DEBOUNCE_MS) && (doorRaw != doorStable)) {
    doorStable = doorRaw;
    doorOpen   = doorStable;
    if (doorStable) {
      Serial.println("[Indoor] door opened");
      sm.onDoorOpened();
    } else {
      Serial.println("[Indoor] door closed");
      sm.onDoorClosed();
    }
  }
}

// ── Actuators ─────────────────────────────────────────────────────────────────

static void updateActuators() {
  SystemState s = sm.getState();
  bool ledOn = (s == SystemState::PREPARE_TO_LEAVE ||
                s == SystemState::PREPARE_TO_ENTER ||
                s == SystemState::LEAVING_HOME      ||
                s == SystemState::ENTERING_HOME     ||
                s == SystemState::UNKNOWN_VISITOR   ||
                s == SystemState::ALERT_MODE);
  digitalWrite(PIN_LED,    ledOn ? HIGH : LOW);
  digitalWrite(PIN_BUZZER, (s == SystemState::ALERT_MODE) ? HIGH : LOW);
}

// ── Arduino lifecycle ─────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  Serial.println("[Indoor] boot");
  Serial.println("[Indoor] WARNING: Dashboard HTTP only — LAN use only, not internet-safe.");

  pinMode(PIN_LED,    OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_DOOR,   INPUT);  // GPIO 34: input-only, external 10kΩ pull-up required
  digitalWrite(PIN_LED,    LOW);
  digitalWrite(PIN_BUZZER, LOW);

  // Sync initial door state to avoid spurious event on first loop
  doorRaw    = (digitalRead(PIN_DOOR) == LOW);
  doorStable = doorRaw;
  doorOpen   = doorRaw;

  // 2a: ConfigPortal — blocks until WiFi STA is connected
  IPAddress localIp, gateway, subnet;
  localIp.fromString(IP_INDOOR);
  gateway.fromString(IP_GATEWAY);
  subnet.fromString(IP_SUBNET);
  ConfigPortal::begin("DualCam-Indoor-Setup", localIp, gateway, subnet);

  Serial.printf("[Indoor] WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());

  // 2c-2d: Settings, Auth, HTTP routes
  SettingsStore::init();
  SessionAuth::begin(server);
  AgentProtocol::registerRoutes(server, sm, "Indoor");
  DashboardServer::begin(server, sm, "Indoor", &cachedPeer, &doorOpen);

  // 2e (hook): state event triggers Discord on UNKNOWN_VISITOR / ALERT_MODE
  sm.setEventCallback(onStateEvent);

  server.begin();
  Serial.printf("[Indoor] HTTP server on port %d\n", HTTP_PORT);
  Serial.println("[Indoor] ready — keys: f=face, u=unknown, a=alert");
}

void loop() {
  server.handleClient();

  // 2b: Peer query every PEER_QUERY_INTERVAL_MS
  if (millis() - lastPeerQuery >= PEER_QUERY_INTERVAL_MS) {
    AgentProtocol::queryPeer(IP_OUTDOOR, "/outside_status", cachedPeer);
    lastPeerQuery = millis();
  }

  handleSerialInput();
  handleDoorSensor();
  sm.tick();
  updateActuators();
}
