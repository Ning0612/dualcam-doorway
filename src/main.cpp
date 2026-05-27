#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <time.h>

#include "config.h"
#include "pins.h"
#include "states.h"
#include "messages.h"

#include "ConfigPortal.h"
#include "SettingsStore.h"
#include "ConfigManager.h"
#include "SessionAuth.h"
#include "DoorSensor.h"
#include "LedController.h"
#include "BuzzerController.h"
#include "SecurityStateMachine.h"
#include "AgentComm.h"
#include "DiscordNotifier.h"
#include "DashboardServer.h"
#include "LogManager.h"
#include "CameraAgent.h"
#include "FaceRecognizer.h"
#include "FaceVoter.h"

// ── Globals ───────────────────────────────────────────────────────────────────

WebServer           server(HTTP_PORT);
SecurityStateMachine sm;
FaceVoter           faceVoter;
LogManager          logManager;

static unsigned long wifiLostMs       = 0;
static unsigned long lastStatusPubMs  = 0;
static constexpr unsigned long STATUS_PUB_INTERVAL_MS = 30000UL;

// ── NTP ───────────────────────────────────────────────────────────────────────

static void syncNtp() {
  configTime(8 * 3600, 0, "pool.ntp.org", "time.cloudflare.com");
  Serial.print("[Agent1] NTP sync");
  for (int i = 0; i < 20; i++) {
    if (time(nullptr) > 1700000000UL) { Serial.println(" OK"); return; }
    delay(500);
    Serial.print('.');
  }
  Serial.println(" FAILED — timestamps will be relative");
}

// ── WiFi loss monitoring ──────────────────────────────────────────────────────

static void handleWifiLoss() {
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiLostMs == 0) {
      wifiLostMs = millis();
      Serial.println("[Agent1] WiFi disconnected, monitoring for recovery.");
    } else if (millis() - wifiLostMs >= WIFI_LOST_TIMEOUT_MS) {
      Serial.println("[Agent1] WiFi lost too long — restarting to config portal.");
      ESP.restart();
    }
  } else {
    if (wifiLostMs != 0) Serial.println("[Agent1] WiFi reconnected.");
    wifiLostMs = 0;
  }
}

// ── SecurityStateMachine callbacks ───────────────────────────────────────────

static void onAlert(AlertLevel level, const char* eventType) {
  Serial.printf("[Agent1] alert %s: %s\n", alertLevelToString(level), eventType);

  if (level == AlertLevel::ALERT_RED) {
    BuzzerController::trigger();

    // Publish escalation so Agent 2 sees the final alarm state (e.g., after decision timeout)
    AgentComm::publishAlert(level, eventType);

    String url = SettingsStore::getDiscordUrl();
    if (url.length() > 0) {
      String msg = String("[Agent1] Unknown visitor detected! Alert: ") + alertLevelToString(level);

      uint8_t* jpegBuf = nullptr;
      size_t   jpegLen = 0;
      bool photoOk = CameraAgent::captureJpeg(&jpegBuf, &jpegLen);

      bool sent = false;
      if (photoOk) {
        sent = DiscordNotifier::notifyWithPhoto(url, AlertEvent::UNKNOWN_VISITOR, msg, jpegBuf, jpegLen);
        free(jpegBuf);
        if (!sent) {
          // Best-effort text fallback if photo upload failed (may be blocked by cooldown)
          sent = DiscordNotifier::notify(url, AlertEvent::UNKNOWN_VISITOR, msg);
        }
      } else {
        sent = DiscordNotifier::notify(url, AlertEvent::UNKNOWN_VISITOR, msg);
      }
      logManager.logAlert(level, eventType, AlarmDecision::TRIGGER_ALARM, sent);
    } else {
      logManager.logAlert(level, eventType, AlarmDecision::TRIGGER_ALARM, false);
    }
  } else if (level == AlertLevel::ALERT_YELLOW) {
    // Yellow: notify Agent 2 via MQTT and wait for decision
    AgentComm::publishAlert(level, eventType);
    logManager.logAlert(level, eventType, AlarmDecision::NO_ACTION, false);
  }
}

static void onDoorEvent(DoorState state, const char* relatedUser) {
  Serial.printf("[Agent1] door %s (user: %s)\n",
                doorStateToString(state), relatedUser[0] ? relatedUser : "none");
  logManager.logDoor(state, relatedUser[0] ? relatedUser : nullptr);
  AgentComm::publishDoor(state, relatedUser[0] ? relatedUser : nullptr);

  if (state == DoorState::DOOR_OPEN && relatedUser[0]) {
    // Known user returned home
    String url = SettingsStore::getDiscordUrl();
    if (url.length() > 0) {
      String msg = String("[Agent1] ") + relatedUser + " 回家";
      DiscordNotifier::notify(url, AlertEvent::USER_RETURNED, msg);
    }
  }
}

static void onKnownConfirmed(const char* name, float similarity) {
  Serial.printf("[Agent1] face KNOWN_CONFIRMED: %s (sim=%.3f)\n", name, similarity);
  logManager.logFace(FaceState::FACE_KNOWN, VoteResult::KNOWN_CONFIRMED, name, similarity);
  AgentComm::publishFace(name, similarity);

  // LED is handled by updateLed() in loop(); buzzer is silenced by _onBuzzerSilence callback.
}

static void onBuzzerSilence() {
  BuzzerController::cancel();
  Serial.println("[Agent1] buzzer silenced");
}

static void onAlarmCancelled() {
  Serial.println("[Agent1] alarm cancelled");
  BuzzerController::cancel();   // safety: ensure off even if already auto-silenced
  // LED is handled by updateLed() in loop().
}

// ── AgentComm callbacks ───────────────────────────────────────────────────────

static void onPresence(bool occupied, int score) {
  Serial.printf("[Agent1] presence: %s (score=%d)\n",
                occupied ? "OCCUPIED" : "UNOCCUPIED", score);
  sm.onPresence(occupied);
  // LED is handled by updateLed() in loop().
}

static void onAlarmDecision(AlarmDecision decision) {
  const char* dstr = (decision == AlarmDecision::TRIGGER_ALARM) ? "TRIGGER_ALARM" :
                     (decision == AlarmDecision::CANCEL_ALARM)  ? "CANCEL_ALARM"  : "NO_ACTION";
  Serial.printf("[Agent1] alarm_decision: %s (from Agent2)\n", dstr);

  // Delegate to state machine. TRIGGER_ALARM is guarded by _waitingForDecision so
  // replayed messages have no effect. CANCEL_ALARM is accepted while _alarmActive;
  // retained CANCEL messages remain a known protocol-level risk without timestamps.
  // SSM fires callbacks: _onAlert for accepted TRIGGER, _onAlarmCancelled for CANCEL.
  sm.onAlarmDecision(decision);
}

static void onAgent2Connection(bool connected) {
  Serial.printf("[Agent1] Agent2 MQTT %s\n", connected ? "connected" : "disconnected");
  sm.onAgent2Online(connected);
  // LED is handled by updateLed() in loop().
}

// ── LED synthesis ─────────────────────────────────────────────────────────────
//
// Single source of truth for LED state. Priority:
//   1. Alarm active  → red blinking
//   2. ALERT_GREEN   → green solid (known-confirmed 15 s window)
//   3. Face detected → white solid (fill light for recognition)
//   4. Idle          → off
//
// Called every loop() after all state updates; only writes to hardware when
// the desired mode changes (avoids NeoPixel show() on every tick).
static void updateLed() {
  enum class LedWant : uint8_t { Off, White, Green, RedBlink };

  LedWant want;
  if (sm.isAlarmActive()) {
    want = LedWant::RedBlink;
  } else if (sm.getAlertLevel() == AlertLevel::ALERT_GREEN) {
    want = LedWant::Green;
  } else if (CameraAgent::isInitialized()) {
    FaceResult raw = CameraAgent::lastRawResult();
    // Only light up for actual face presence; ignore NONE and CAMERA_ERROR.
    if (raw == FaceResult::DETECTED || raw == FaceResult::KNOWN || raw == FaceResult::UNKNOWN) {
      want = LedWant::White;
    } else {
      want = LedWant::Off;
    }
  } else {
    want = LedWant::Off;
  }

  static LedWant prevWant    = LedWant::Off;
  static bool    initialized = false;
  if (initialized && want == prevWant) return;
  initialized = true;
  prevWant = want;

  switch (want) {
    case LedWant::RedBlink:
      LedController::setLevel(AlertLevel::ALERT_RED);
      LedController::setBlinking(true);
      break;
    case LedWant::Green:
      LedController::setLevel(AlertLevel::ALERT_GREEN);
      LedController::setBlinking(false);
      break;
    case LedWant::White:
      LedController::setWhite();
      break;
    case LedWant::Off:
      LedController::setOff();
      break;
  }
}

// ── DoorSensor callback ───────────────────────────────────────────────────────

static void onDoorChange(DoorState state) {
  Serial.printf("[Agent1] door %s\n", doorStateToString(state));
  sm.onDoorChange(state);
}

// ── Serial input ──────────────────────────────────────────────────────────────

static void handleSerialInput() {
  if (!Serial.available()) return;
  char c = Serial.read();

  switch (c) {
    case 'h':
      Serial.printf("[Agent1] Hall raw=%u  lo=%u  hi=%u  hyst=±%d  door=%s\n",
                    DoorSensor::getRaw(),
                    SettingsStore::getHallLowerBound(),
                    SettingsStore::getHallUpperBound(),
                    HALL_HYSTERESIS,
                    DoorSensor::getState() == DoorState::DOOR_OPEN ? "OPEN" : "CLOSED");
      break;
    case 'H': {
      // Press while door is CLOSED (magnet engaged). Auto-detects deflection direction
      // and offsets the bound by 2*hyst so the closed reading is clearly outside the open zone.
      uint16_t raw = DoorSensor::getRaw();
      if (raw == 2048) {
        Serial.println("[Agent1] Hall raw too close to midpoint; engage magnet then retry.");
        break;
      }
      uint16_t lo = SettingsStore::getHallLowerBound();
      uint16_t hi = SettingsStore::getHallUpperBound();
      if (raw > 2048) {
        // Positive deflection: update upper bound
        int new_hi = (int)raw - 2 * (int)HALL_HYSTERESIS;
        if (new_hi <= (int)lo + 2 * (int)HALL_HYSTERESIS) {
          Serial.printf("[Agent1] Hall calibration rejected: raw=%u too close to lower bound.\n", raw);
          break;
        }
        hi = (uint16_t)new_hi;
      } else {
        // Negative deflection: update lower bound
        int new_lo = (int)raw + 2 * (int)HALL_HYSTERESIS;
        if (new_lo >= (int)hi - 2 * (int)HALL_HYSTERESIS) {
          Serial.printf("[Agent1] Hall calibration rejected: raw=%u too close to upper bound.\n", raw);
          break;
        }
        lo = (uint16_t)new_lo;
      }
      if (!SettingsStore::setHallBounds(lo, hi)) {
        Serial.printf("[Agent1] Hall bounds rejected: lo=%u hi=%u\n", lo, hi);
      } else {
        DoorSensor::setBounds(lo, hi);
        Serial.printf("[Agent1] Hall bounds saved: lo=%u hi=%u\n", lo, hi);
      }
      break;
    }
    case 'd':
      // Simulate door toggle for testing (without hardware)
      if (sm.getDoorState() == DoorState::DOOR_CLOSED) {
        Serial.println("[Agent1] door OPEN (manual)");
        sm.onDoorChange(DoorState::DOOR_OPEN);
      } else {
        Serial.println("[Agent1] door CLOSED (manual)");
        sm.onDoorChange(DoorState::DOOR_CLOSED);
      }
      break;
    case 'u':
      Serial.println("[Agent1] unknown visitor CONFIRMED (manual)");
      sm.onVoteResult(VoteResult::UNKNOWN_CONFIRMED);
      break;
    case 'e':
      if (!CameraAgent::isInitialized()) {
        Serial.println("[Agent1] camera not ready — enroll unavailable");
      } else if (FaceRecognizer::count() >= FaceRecognizer::MAX_FACES) {
        Serial.printf("[Agent1] face bank full (%d/%d) — clear with 'r'\n",
                      FaceRecognizer::count(), FaceRecognizer::MAX_FACES);
      } else {
        CameraAgent::scheduleEnroll();
        Serial.printf("[Agent1] enroll scheduled (%d/%d enrolled)\n",
                      FaceRecognizer::count(), FaceRecognizer::MAX_FACES);
      }
      break;
    case 'r':
      CameraAgent::cancelEnroll();
      FaceRecognizer::clearAll();
      break;
    case 'n':
      Serial.printf("[Agent1] enrolled faces: %d/%d\n",
                    FaceRecognizer::count(), FaceRecognizer::MAX_FACES);
      break;
    case 'c':
      Serial.printf("[Agent1] camera: init=%s lastDet=%lums\n",
                    CameraAgent::isInitialized() ? "OK" : "FAIL",
                    CameraAgent::lastDetectedMs());
      break;
    case 's':
      Serial.printf("[Agent1] alert=%s door=%s agent2=%s alarm=%s\n",
                    alertLevelToString(sm.getAlertLevel()),
                    doorStateToString(sm.getDoorState()),
                    sm.isAgent2Online() ? "online" : "offline",
                    sm.isAlarmActive() ? "ACTIVE" : "off");
      break;
    case 'W':
      Serial.println("[Agent1] Clearing WiFi credentials and restarting...");
      if (ConfigPortal::clearCredentials()) ESP.restart();
      else Serial.println("[Agent1] Clear failed — NOT restarting.");
      break;
  }
}

// ── Arduino lifecycle ─────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  Serial.println("[Agent1] boot");
  Serial.println("[Agent1] WARNING: Dashboard HTTP only — LAN use only, not internet-safe.");

  // Actuators first (visual boot indicator)
  LedController::begin(PIN_LED_DATA);
  BuzzerController::begin(PIN_BUZZER, BUZZER_DEFAULT_FREQ_HZ);

  // WiFi provisioning — blocks until connected or 5-min AP timeout
  ConfigPortal::begin("Agent1-Setup");

  Serial.printf("[Agent1] WiFi connected. IP: %s  MAC: %s\n",
                WiFi.localIP().toString().c_str(),
                WiFi.macAddress().c_str());

  if (!MDNS.begin(MDNS_AGENT1)) {
    Serial.println("[Agent1] WARNING: mDNS start failed.");
  } else {
    Serial.printf("[Agent1] mDNS: %s.local\n", MDNS_AGENT1);
  }

  syncNtp();

  // Settings & config
  SettingsStore::init();
  ConfigManager::begin();
  BuzzerController::setFrequency(ConfigManager::getBuzzerFreq());
  sm.setBuzzerDuration(ConfigManager::getBuzzerDurationMs());

  // Hall sensor
  uint16_t hallLo = SettingsStore::getHallLowerBound();
  uint16_t hallHi = SettingsStore::getHallUpperBound();
  Serial.printf("[Agent1] Hall bounds: lo=%u hi=%u\n", hallLo, hallHi);
  DoorSensor::begin(PIN_HALL, hallLo, hallHi, HALL_HYSTERESIS);
  DoorSensor::setOnChange(onDoorChange);

  // Log system
  logManager.begin();

  // State machine callbacks
  sm.setOnAlert(onAlert);
  sm.setOnDoorEvent(onDoorEvent);
  sm.setOnKnownConfirmed(onKnownConfirmed);
  sm.setOnAlarmCancelled(onAlarmCancelled);
  sm.setOnBuzzerSilence(onBuzzerSilence);

  // Face recognition
  FaceRecognizer::begin();
  FaceRecognizer::setOnClearCallback([]{ faceVoter.reset(); });

  // HTTP server
  SessionAuth::begin(server);
  DashboardServer::begin(server, sm, "Agent1", &faceVoter, &logManager);
  server.begin();
  Serial.printf("[Agent1] HTTP server on port %d\n", HTTP_PORT);

  // MQTT (may be unconfigured; AgentComm handles empty broker gracefully)
  AgentComm::setOnPresence(onPresence);
  AgentComm::setOnAlarmDecision(onAlarmDecision);
  AgentComm::setOnConnectionChange(onAgent2Connection);
  AgentComm::begin(ConfigManager::getMqttBroker().c_str(), ConfigManager::getMqttPort());

  // Discord boot notification (async, non-blocking)
  if (xTaskCreate([](void*) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    String url = SettingsStore::getDiscordUrl();
    if (url.length() > 0) {
      String ip = WiFi.localIP().toString();
      DiscordNotifier::notifyBoot(url, "[Agent1] Online — http://" + ip);
    }
    vTaskDelete(nullptr);
  }, "boot_notify", 8192, nullptr, 1, nullptr) != pdPASS) {
    Serial.println("[Agent1] WARNING: boot_notify task creation failed.");
  }

  // Camera init (async, keeps loop() unblocked during esp_camera_init())
  xTaskCreate([](void*) {
    if (!CameraAgent::begin()) {
      Serial.println("[Agent1] WARNING: camera init failed — face detection unavailable");
    } else {
      CameraAgent::startStreamServer();  // MJPEG on port 81
    }
    vTaskDelete(nullptr);
  }, "cam_init", 8192, nullptr, 1, nullptr);

  Serial.println("[Agent1] ready");
  Serial.println("[Agent1]   h=Hall value  H=save threshold  d=door toggle");
  Serial.println("[Agent1]   u=unknown     e=enroll face     r=clear faces");
  Serial.println("[Agent1]   n=face count  c=camera status   s=full status");
  Serial.println("[Agent1]   W=clear WiFi credentials");
}

void loop() {
  server.handleClient();
  CameraAgent::handleStreamClients();

  DoorSensor::tick();
  LedController::tick();
  BuzzerController::tick();
  AgentComm::tick();

  // Face recognition pipeline → FaceVoter → SecurityStateMachine
  FaceResult edge = CameraAgent::tick();
  (void)edge;  // edge-trigger path not used; FaceVoter handles sustained detection

  VoteResult vote = faceVoter.update(
    CameraAgent::lastRawResult(),
    CameraAgent::lastRawResultMs(),
    millis()
  );

  if (vote == VoteResult::KNOWN_CONFIRMED) {
    faceVoter.setConfirmedName(FaceRecognizer::getLastMatchName());
    sm.onVoteResult(VoteResult::KNOWN_CONFIRMED,
                    FaceRecognizer::getLastMatchName(),
                    FaceRecognizer::getLastSim());
  } else if (vote == VoteResult::UNKNOWN_CONFIRMED) {
    Serial.println("[Agent1] unknown visitor confirmed by vote window");
    logManager.logFace(FaceState::FACE_UNKNOWN, VoteResult::UNKNOWN_CONFIRMED, "", 0.0f);
    sm.onVoteResult(VoteResult::UNKNOWN_CONFIRMED);
  }

  // Periodic MQTT status heartbeat
  if (millis() - lastStatusPubMs >= STATUS_PUB_INTERVAL_MS) {
    AgentComm::publishStatus(sm.getAlertLevel(), millis());
    lastStatusPubMs = millis();
  }

  sm.tick();
  updateLed();
  handleWifiLoss();
  handleSerialInput();
}
