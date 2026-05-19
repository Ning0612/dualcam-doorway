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
#include "CameraAgent.h"

// ── Globals ───────────────────────────────────────────────────────────────────

WebServer        server(HTTP_PORT);
DoorStateMachine sm;
PeerStatus       cachedPeer = {false, SystemState::IDLE, 0};

static unsigned long lastPeerQuery = 0;
static unsigned long lastBlinkMs   = 0;
static bool          ledState      = false;

static unsigned long wifiLostMs = 0;

// ── WiFi loss monitoring ──────────────────────────────────────────────────────
// Resets on any successful reconnect. If WiFi flaps briefly but recovers, the
// 5-min timer resets intentionally — a device that can reconnect should stay up.
// Only a continuous 5-min blackout triggers a restart into portal mode.

static void handleWifiLoss() {
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiLostMs == 0) {
      wifiLostMs = millis();
      Serial.println("[Outdoor] WiFi disconnected, monitoring for recovery.");
    } else if (millis() - wifiLostMs >= WIFI_LOST_TIMEOUT_MS) {
      Serial.println("[Outdoor] WiFi lost too long — restarting to config portal.");
      ESP.restart();
    }
  } else {
    if (wifiLostMs != 0) Serial.println("[Outdoor] WiFi reconnected.");
    wifiLostMs = 0;
  }
}

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
  // Manual overrides for testing (camera provides real events in normal operation)
  if (c == 'u') {
    Serial.println("[Outdoor] unknown visitor (manual override)");
    sm.onUnknownVisitor();
  } else if (c == 'a') {
    Serial.println("[Outdoor] alert triggered (manual override)");
    sm.onAlert();
  } else if (c == 'c') {
    Serial.printf("[Outdoor] camera: init=%s lastDet=%lums\n",
                  CameraAgent::isInitialized() ? "OK" : "FAIL",
                  CameraAgent::lastDetectedMs());
  } else if (c == 'W') {
    Serial.println("[Outdoor] Clearing WiFi credentials and restarting...");
    if (ConfigPortal::clearCredentials()) ESP.restart();
    else Serial.println("[Outdoor] Clear failed — NOT restarting.");
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

  // Phase 4: camera init in background task — keeps loop() unblocked
  xTaskCreate([](void*) {
    if (!CameraAgent::begin()) {
      Serial.println("[Outdoor] WARNING: camera init failed — face detection unavailable");
    } else {
      CameraAgent::startStreamServer();  // MJPEG on port 81
    }
    vTaskDelete(nullptr);
  }, "cam_init", 8192, nullptr, 1, nullptr);

  Serial.println("[Outdoor] ready — keys: u=unknown, a=alert, c=camera status, W=clear WiFi");
}

void loop() {
  server.handleClient();
  CameraAgent::handleStreamClients();

  // 2b: Peer query every PEER_QUERY_INTERVAL_MS
  if (millis() - lastPeerQuery >= PEER_QUERY_INTERVAL_MS) {
    AgentProtocol::queryPeer(IP_INDOOR, "/home_state", cachedPeer);
    lastPeerQuery = millis();
  }

  // Phase 4: feed camera detection into state machine
  // Outdoor agent: detected face from camera = outdoor face event
  FaceResult face = CameraAgent::tick();
  if (face == FaceResult::DETECTED) {
    sm.onOutdoorFaceDetected();
  }

  handleWifiLoss();
  handleSerialInput();
  sm.tick();
  updateActuators();
}
