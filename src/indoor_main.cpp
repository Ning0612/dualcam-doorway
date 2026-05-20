#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
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
#include "FaceRecognizer.h"

// ── Globals ───────────────────────────────────────────────────────────────────

WebServer        server(HTTP_PORT);
DoorStateMachine sm;
PeerStatus       cachedPeer = {false, SystemState::IDLE, 0};

static bool          doorOpen         = false;
static uint16_t      hallRaw          = 0;
static uint16_t      hallThreshold    = HALL_DEFAULT_THRESHOLD;
static unsigned long lastHallMs       = 0;
static unsigned long lastPeerQuery    = 0;

// Debounce state for Hall sensor transitions
static bool          doorPendingActive = false;
static bool          doorPendingState  = false;
static unsigned long doorPendingMs     = 0;

static unsigned long wifiLostMs = 0;

// ── WiFi loss monitoring ──────────────────────────────────────────────────────
// Resets on any successful reconnect. If WiFi flaps briefly but recovers, the
// 5-min timer resets intentionally — a device that can reconnect should stay up.
// Only a continuous 5-min blackout triggers a restart into portal mode.

static void handleWifiLoss() {
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiLostMs == 0) {
      wifiLostMs = millis();
      Serial.println("[Indoor] WiFi disconnected, monitoring for recovery.");
    } else if (millis() - wifiLostMs >= WIFI_LOST_TIMEOUT_MS) {
      Serial.println("[Indoor] WiFi lost too long — restarting to config portal.");
      ESP.restart();
    }
  } else {
    if (wifiLostMs != 0) Serial.println("[Indoor] WiFi reconnected.");
    wifiLostMs = 0;
  }
}

// ── State event callback ──────────────────────────────────────────────────────

static void onStateEvent(const StateEvent& ev) {
  Serial.print("[Indoor] state -> ");
  Serial.println(stateToString(ev.to));

  if (ev.to == SystemState::UNKNOWN_VISITOR || ev.to == SystemState::ALERT_MODE) {
    String url = SettingsStore::getDiscordUrl();
    if (url.length() > 0) {
      String msg = String("[DualCam Indoor] ") + stateToString(ev.to);
      DiscordNotifier::notify(url, ev.to, msg);
    } else {
      Serial.println("[Indoor] WARNING: Discord URL not configured; skipping notification.");
    }
  }
}

// ── Hall-effect door sensor ───────────────────────────────────────────────────
//
// Reads 8 samples and averages to reduce ADC noise.
// State transitions use hysteresis to prevent rapid toggling near the threshold:
//   hallRaw < (threshold - HALL_HYSTERESIS)  → door OPEN
//   hallRaw > (threshold + HALL_HYSTERESIS)  → door CLOSED
//   otherwise                                → dead zone, keep current state
//
// Calibration:
//   - Serial 'h': print current raw value and threshold
//   - Serial 'H': save current raw value as new threshold

static void handleDoorSensor() {
  if (millis() - lastHallMs < HALL_SAMPLE_INTERVAL_MS) return;
  lastHallMs = millis();

  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(PIN_HALL);
  hallRaw = (uint16_t)(sum >> 3);

  // Hysteresis: determine if we're clearly in OPEN or CLOSED territory
  bool shouldBeOpen;
  if      (hallRaw < (uint16_t)(hallThreshold - HALL_HYSTERESIS))
    shouldBeOpen = true;
  else if (hallRaw > (uint16_t)(hallThreshold + HALL_HYSTERESIS))
    shouldBeOpen = false;
  else {
    // Dead zone — clear any pending debounce and keep current state
    doorPendingActive = false;
    return;
  }

  if (shouldBeOpen == doorOpen) {
    doorPendingActive = false;  // already in correct state
    return;
  }

  // Debounce: require stable new state for DOOR_DEBOUNCE_MS before firing
  if (!doorPendingActive || doorPendingState != shouldBeOpen) {
    doorPendingActive = true;
    doorPendingState  = shouldBeOpen;
    doorPendingMs     = millis();
    return;
  }

  if (millis() - doorPendingMs < DOOR_DEBOUNCE_MS) return;

  // Debounce period elapsed — commit transition
  doorPendingActive = false;
  doorOpen          = shouldBeOpen;
  if (doorOpen) {
    Serial.println("[Indoor] door opened");
    sm.onDoorOpened();
  } else {
    Serial.println("[Indoor] door closed");
    sm.onDoorClosed();
  }
}

// ── Input handling ────────────────────────────────────────────────────────────

static void handleSerialInput() {
  if (!Serial.available()) return;
  char c = Serial.read();

  if (c == 'h') {
    Serial.printf("[Indoor] Hall raw=%u  threshold=%u  hysteresis=±%d  door=%s\n",
                  hallRaw, hallThreshold, HALL_HYSTERESIS,
                  doorOpen ? "OPEN" : "CLOSED");
  } else if (c == 'H') {
    if (!SettingsStore::setHallThreshold(hallRaw)) {
      Serial.printf("[Indoor] Hall threshold rejected: %u is outside valid range [%d, %d]\n",
                    hallRaw, HALL_HYSTERESIS + 1, 4095 - HALL_HYSTERESIS);
    } else {
      hallThreshold = hallRaw;
      Serial.printf("[Indoor] Hall threshold saved: %u (NVS updated)\n", hallThreshold);
    }
  } else if (c == 'u') {
    Serial.println("[Indoor] unknown visitor (manual override)");
    SystemState before = sm.getState();
    sm.onUnknownVisitor();
    if (sm.getState() == before)
      Serial.printf("[Indoor] HINT: ignored -- need IDLE or PREPARE_TO_ENTER (current: %s)\n",
                    stateToString(before));
  } else if (c == 'a') {
    Serial.println("[Indoor] alert triggered (manual override)");
    SystemState before = sm.getState();
    sm.onAlert();
    if (sm.getState() == before)
      Serial.printf("[Indoor] HINT: ignored -- need UNKNOWN_VISITOR (current: %s). Press 'u' first.\n",
                    stateToString(before));
  } else if (c == 'c') {
    Serial.printf("[Indoor] camera: init=%s lastDet=%lums\n",
                  CameraAgent::isInitialized() ? "OK" : "FAIL",
                  CameraAgent::lastDetectedMs());
  } else if (c == 'e') {
    if (!CameraAgent::isInitialized()) {
      Serial.println("[Indoor] camera not ready — enroll unavailable");
    } else if (FaceRecognizer::count() >= FaceRecognizer::MAX_FACES) {
      Serial.printf("[Indoor] face bank full (%d/%d) — clear first with 'r'\n",
                    FaceRecognizer::count(), FaceRecognizer::MAX_FACES);
    } else {
      CameraAgent::scheduleEnroll();
      Serial.printf("[Indoor] enroll scheduled (%d/%d enrolled)\n",
                    FaceRecognizer::count(), FaceRecognizer::MAX_FACES);
    }
  } else if (c == 'r') {
    CameraAgent::cancelEnroll();
    FaceRecognizer::clearAll();
  } else if (c == 'n') {
    Serial.printf("[Indoor] enrolled faces: %d/%d\n",
                  FaceRecognizer::count(), FaceRecognizer::MAX_FACES);
  } else if (c == 'W') {
    Serial.println("[Indoor] Clearing WiFi credentials and restarting...");
    if (ConfigPortal::clearCredentials()) ESP.restart();
    else Serial.println("[Indoor] Clear failed — NOT restarting.");
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
  Serial.println("[Indoor] WARNING: Dashboard HTTP only -- LAN use only, not internet-safe.");

  pinMode(PIN_LED,    OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  // PIN_HALL (GPIO 33) is analog input — no pinMode needed.
  // Set full-range attenuation: 0-3.3V maps to 0-4095.
  analogSetPinAttenuation(PIN_HALL, ADC_11db);
  digitalWrite(PIN_LED,    LOW);
  digitalWrite(PIN_BUZZER, LOW);

  // 2a: ConfigPortal — blocks until WiFi STA is connected (DHCP)
  ConfigPortal::begin("DualCam-Indoor-Setup");

  Serial.printf("[Indoor] WiFi connected. IP: %s  MAC: %s\n",
                WiFi.localIP().toString().c_str(),
                WiFi.macAddress().c_str());

  if (!MDNS.begin(MDNS_INDOOR)) {
    Serial.println("[Indoor] WARNING: mDNS start failed — peer discovery may not work.");
  } else {
    Serial.printf("[Indoor] mDNS: %s.local\n", MDNS_INDOOR);
  }

  // 2c-2d: Settings, Auth, HTTP routes
  SettingsStore::init();
  hallThreshold = SettingsStore::getHallThreshold();
  Serial.printf("[Indoor] Hall threshold loaded: %u\n", hallThreshold);

  SessionAuth::begin(server);
  AgentProtocol::registerRoutes(server, sm, "Indoor");
  DashboardServer::begin(server, sm, "Indoor", &cachedPeer, &doorOpen, &hallRaw, &hallThreshold);

  // 2e: state event triggers Discord on UNKNOWN_VISITOR / ALERT_MODE
  sm.setEventCallback(onStateEvent);

  // Sync initial door state from Hall sensor before server starts
  {
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += analogRead(PIN_HALL);
    hallRaw  = (uint16_t)(sum >> 3);
    doorOpen = (hallRaw < hallThreshold);
    Serial.printf("[Indoor] Hall initial: raw=%u threshold=%u door=%s\n",
                  hallRaw, hallThreshold, doorOpen ? "OPEN" : "CLOSED");
  }

  // Load enrolled faces synchronously so /api/status face_count is correct from first request
  FaceRecognizer::begin();

  server.begin();
  Serial.printf("[Indoor] HTTP server on port %d\n", HTTP_PORT);

  // Announce IP on Discord asynchronously so setup() is not blocked by HTTPS timeout.
  // Delayed 5 s to avoid simultaneous heap competition with cam_init during boot.
  // notifyBoot() intentionally does not touch _failCooldownUntil, keeping security
  // alert channels unaffected even if the boot message fails.
  if (xTaskCreate([](void*) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    String url = SettingsStore::getDiscordUrl();
    if (url.length() > 0) {
      String ip = WiFi.localIP().toString();
      DiscordNotifier::notifyBoot(url, "<Indoor> IP: http://" + ip);
    }
    vTaskDelete(nullptr);
  }, "boot_notify", 8192, nullptr, 1, nullptr) != pdPASS) {
    Serial.println("[Indoor] WARNING: boot_notify task creation failed.");
  }

  // Phase 4: camera init runs in a background task so loop() is never blocked.
  // esp_camera_init() can stall on I2C if the module is absent or mis-wired;
  // running it off the main core keeps the HTTP server responsive regardless.
  xTaskCreate([](void*) {
    if (!CameraAgent::begin()) {
      Serial.println("[Indoor] WARNING: camera init failed — face detection unavailable");
    } else {
      CameraAgent::startStreamServer();  // MJPEG on port 81
    }
    vTaskDelete(nullptr);
  }, "cam_init", 8192, nullptr, 1, nullptr);

  Serial.println("[Indoor] ready");
  Serial.println("[Indoor]   h = show Hall value/threshold");
  Serial.println("[Indoor]   H = save current reading as threshold");
  Serial.println("[Indoor]   u = unknown visitor  a = alert  c = camera status");
  Serial.println("[Indoor]   e = enroll next face  r = clear faces  n = face count");
  Serial.println("[Indoor]   W = clear WiFi credentials and restart to config portal");
}

void loop() {
  server.handleClient();
  CameraAgent::handleStreamClients();

  // 2b: Peer query every PEER_QUERY_INTERVAL_MS
  if (millis() - lastPeerQuery >= PEER_QUERY_INTERVAL_MS) {
    AgentProtocol::queryPeer(MDNS_OUTDOOR, "/outside_status", cachedPeer);
    lastPeerQuery = millis();
  }

  // Phase 5: any face detected indoors (known or unknown) means someone is leaving
  FaceResult face = CameraAgent::tick();
  if (face == FaceResult::DETECTED || face == FaceResult::KNOWN || face == FaceResult::UNKNOWN) {
    sm.onIndoorFaceDetected();
  }

  handleWifiLoss();
  handleSerialInput();
  handleDoorSensor();
  sm.tick();
  updateActuators();
}
