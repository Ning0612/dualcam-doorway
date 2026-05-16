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

static unsigned long lastPeerQuery = 0;
static unsigned long lastBlinkMs   = 0;
static bool          ledState      = false;

// ── State event callback ──────────────────────────────────────────────────────

static void onStateEvent(const StateEvent& ev) {
  Serial.print("[Outdoor] state -> ");
  Serial.println(stateToString(ev.to));

  if (ev.to == SystemState::UNKNOWN_VISITOR || ev.to == SystemState::ALERT_MODE) {
    String url = SettingsStore::getDiscordUrl();
    if (url.length() > 0) {
      String msg = String("[DualCam Outdoor] ") + stateToString(ev.to);
      DiscordNotifier::notify(url, ev.to, msg);
    }
  }
}

// ── Input handling ────────────────────────────────────────────────────────────

static void handleSerialInput() {
  if (!Serial.available()) return;
  char c = Serial.read();
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
}

// ── Actuators ─────────────────────────────────────────────────────────────────

static void updateActuators() {
  unsigned long now = millis();
  SystemState   s   = sm.getState();

  if (s == SystemState::ALERT_MODE || s == SystemState::UNKNOWN_VISITOR) {
    if (now - lastBlinkMs >= 250) {
      lastBlinkMs = now;
      ledState    = !ledState;
      digitalWrite(PIN_LED, ledState ? HIGH : LOW);
    }
    digitalWrite(PIN_BUZZER, (s == SystemState::ALERT_MODE) ? HIGH : LOW);
  } else {
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

// ── Arduino lifecycle ─────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  Serial.println("[Outdoor] boot");
  Serial.println("[Outdoor] WARNING: Dashboard HTTP only — LAN use only, not internet-safe.");

  pinMode(PIN_LED,    OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_LED,    LOW);
  digitalWrite(PIN_BUZZER, LOW);

  // 2a: ConfigPortal — blocks until WiFi STA is connected
  IPAddress localIp, gateway, subnet;
  localIp.fromString(IP_OUTDOOR);
  gateway.fromString(IP_GATEWAY);
  subnet.fromString(IP_SUBNET);
  ConfigPortal::begin("DualCam-Outdoor-Setup", localIp, gateway, subnet);

  Serial.printf("[Outdoor] WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());

  // 2c-2d: Settings, Auth, HTTP routes
  SettingsStore::init();
  SessionAuth::begin(server);
  AgentProtocol::registerRoutes(server, sm, "Outdoor");
  DashboardServer::begin(server, sm, "Outdoor", &cachedPeer, nullptr);

  // 2e (hook): state event triggers Discord on UNKNOWN_VISITOR / ALERT_MODE
  sm.setEventCallback(onStateEvent);

  server.begin();
  Serial.printf("[Outdoor] HTTP server on port %d\n", HTTP_PORT);
  Serial.println("[Outdoor] ready — keys: f=face, u=unknown, a=alert");
}

void loop() {
  server.handleClient();

  // 2b: Peer query every PEER_QUERY_INTERVAL_MS
  if (millis() - lastPeerQuery >= PEER_QUERY_INTERVAL_MS) {
    AgentProtocol::queryPeer(IP_INDOOR, "/home_state", cachedPeer);
    lastPeerQuery = millis();
  }

  handleSerialInput();
  sm.tick();
  updateActuators();
}
