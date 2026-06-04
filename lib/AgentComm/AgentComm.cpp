#include "AgentComm.h"
#include "config.h"
#include "messages.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// ── Static storage ────────────────────────────────────────────────────────────

static WiFiClient   _wifiClient;
static PubSubClient _mqtt(_wifiClient);

static char     _broker[64]   = {};
static uint16_t _port         = MQTT_DEFAULT_PORT;
static char     _user[64]     = {};
static char     _pass[64]     = {};
static char     _clientId[24] = "faceguard";
static bool     _configured   = false;

static IPAddress _brokerIP;
static bool      _brokerIPResolved = false;
static uint8_t   _connectFailCount = 0;

// _brokerConnected: written by MQTT task, read by isConnected() on main task.
// portMUX_TYPE critical section guarantees cross-core visibility on ESP32.
static portMUX_TYPE  _brokerStateMux  = portMUX_INITIALIZER_UNLOCKED;
static bool          _brokerConnected = false;

// _agent2Online / _lastPresenceMs: owned exclusively by the MQTT task.
// Main task learns of state changes only through _eventQueue events.
static bool          _agent2Online   = false;
static unsigned long _lastPresenceMs = 0;

static void (*_onPresence)(bool, int)          = nullptr;
static void (*_onAlarmDecision)(AlarmDecision) = nullptr;
static void (*_onAlarmCommand)(AlarmDecision)  = nullptr;
static void (*_onConnChange)(bool)             = nullptr;

// ── Cross-task event queue (MQTT task → main task) ────────────────────────────

struct CommEvent {
    uint8_t       type;    // 1=presence, 2=alarm, 3=conn_change
    bool          boolVal; // presence: occupied; conn_change: online
    int           intVal;  // presence: score
    AlarmDecision alarm;
};

static QueueHandle_t     _eventQueue = nullptr;
static SemaphoreHandle_t _mqttMutex  = nullptr;
static TaskHandle_t      _mqttTask   = nullptr;

// critical=true: log a warning if the queue is full (alarm and conn_change events).
static void _enqueue(CommEvent ev, bool critical = false) {
    if (!_eventQueue) return;
    if (xQueueSend(_eventQueue, &ev, 0) != pdTRUE && critical) {
        Serial.printf("[FaceGuard] WARNING: AgentComm event queue full (type=%u dropped)\n", ev.type);
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

// Returns true if the ISO 8601 UTC timestamp is within maxAgeSec of now.
// When NTP is unavailable (epoch < 2023), skips validation and returns true.
static bool _cmdIsFresh(const char* isoTs, unsigned maxAgeSec = 5) {
    time_t now = time(nullptr);
    if (now < 1700000000UL) return true;  // NTP not synced — cannot validate

    int yr, mo, dy, hr, mi, se;
    if (sscanf(isoTs, "%4d-%2d-%2dT%2d:%2d:%2d", &yr, &mo, &dy, &hr, &mi, &se) != 6) return false;

    // Days since Unix epoch using Gregorian calendar formula
    static const uint16_t mdays[] = {0,31,59,90,120,151,181,212,243,273,304,334};
    bool leap = (yr % 4 == 0 && yr % 100 != 0) || yr % 400 == 0;
    long d = (long)(yr - 1970) * 365 + (yr - 1969) / 4 - (yr - 1901) / 100 + (yr - 1601) / 400
             + mdays[mo - 1] + (mo > 2 && leap ? 1 : 0) + dy - 1;
    time_t msgEpoch = (time_t)(d * 86400L + hr * 3600L + mi * 60L + se);
    return labs((long)now - (long)msgEpoch) <= (long)maxAgeSec;
}

static String _getTimestamp() {
    time_t now = time(nullptr);
    if (now < 1700000000UL) {
        return String("1970-01-01T00:00:00.000000Z");
    }
    struct tm t;
    gmtime_r(&now, &t);
    char buf[28];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.000000Z", &t);
    return String(buf);
}

static void _appendCommonFields(JsonDocument& doc) {
    doc[MSG_AGENT_ID]  = "agent1";
    doc[MSG_TIMESTAMP] = _getTimestamp();
}

// ── MQTT message handler (runs inside _mqtt.loop(), holds _mqttMutex) ────────

void AgentComm::_onMessage(const char* topic, byte* payload, unsigned int len) {
    if (len == 0 || len > 512) return;

    char buf[513];
    memcpy(buf, payload, len);
    buf[len] = '\0';

    JsonDocument doc;
    if (deserializeJson(doc, buf) != DeserializationError::Ok) return;

    if (strcmp(topic, MQTT_TOPIC_PRESENCE) == 0) {
        const char* ps = doc[MSG_PRESENCE_STATE] | "";
        bool occupied  = (strcmp(ps, PRESENCE_OCCUPIED) == 0);
        int  score     = doc[MSG_PRESENCE_SCORE] | 0;

        _lastPresenceMs = millis();
        if (!_agent2Online) {
            _agent2Online = true;
            _enqueue({3, true, 0, AlarmDecision::NO_ACTION}, true);  // conn_change: online
        }
        _enqueue({1, occupied, score, AlarmDecision::NO_ACTION});    // presence (droppable)

    } else if (strcmp(topic, MQTT_TOPIC_ALARM) == 0) {
        const char* ad = doc[MSG_ALARM_DECISION] | "";
        AlarmDecision decision = AlarmDecision::NO_ACTION;
        if      (strcmp(ad, ALARM_TRIGGER) == 0) decision = AlarmDecision::TRIGGER_ALARM;
        else if (strcmp(ad, ALARM_CANCEL)  == 0) decision = AlarmDecision::CANCEL_ALARM;
        _enqueue({2, false, 0, decision}, true);  // alarm_decision: critical

    } else if (strcmp(topic, MQTT_TOPIC_ALARM_CMD) == 0) {
        // Validate sender and freshness to guard against retained/replayed MQTT messages.
        // alarm_command bypasses _waitingForDecision, so stale retained payloads must be rejected.
        const char* agentId = doc[MSG_AGENT_ID] | "";
        if (strcmp(agentId, "agent2") != 0) return;
        const char* ts = doc[MSG_TIMESTAMP] | "";
        if (!_cmdIsFresh(ts)) {
            Serial.println("[FaceGuard] WARNING: stale alarm_command rejected (retained or replayed)");
            return;
        }
        const char* ad = doc[MSG_ALARM_DECISION] | "";
        AlarmDecision decision = AlarmDecision::NO_ACTION;
        if      (strcmp(ad, ALARM_TRIGGER) == 0) decision = AlarmDecision::TRIGGER_ALARM;
        else if (strcmp(ad, ALARM_CANCEL)  == 0) decision = AlarmDecision::CANCEL_ALARM;
        if (decision != AlarmDecision::NO_ACTION) {
            _enqueue({4, false, 0, decision}, true);  // alarm_command: critical, no guard
        }

    } else if (strcmp(topic, MQTT_TOPIC_DISPLAY_STATUS) == 0) {
        const char* status = doc["status"] | "unknown";
        Serial.printf("[FaceGuard] display/status: %s\n", status);
    }
}

// ── MQTT background task (Core 0) ─────────────────────────────────────────────
//
// Owns all PubSubClient access. The _mqttMutex gate lets _publish() on the main
// task safely interleave without blocking the loop() for DNS-resolution delays.

void AgentComm::_mqttTaskFn(void*) {
    unsigned long lastReconnectMs   = 0;
    bool          prevWifiConnected = false;

    while (true) {
        bool wifiNow = (WiFi.status() == WL_CONNECTED);

        // WiFi just reconnected: invalidate DNS cache (new network may have different IP/mDNS)
        if (wifiNow && !prevWifiConnected) {
            _brokerIPResolved = false;
            _connectFailCount = 0;
        }
        prevWifiConnected = wifiNow;

        if (!_configured || !wifiNow) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        xSemaphoreTake(_mqttMutex, portMAX_DELAY);
        bool connected = _mqtt.connected();
        if (connected) {
            _mqtt.loop();  // may call _onMessage → _enqueue (safe while holding mutex)
        }
        xSemaphoreGive(_mqttMutex);

        if (connected) {
            // Agent 2 timeout — state owned by this task, no cross-core sync needed
            if (_agent2Online && _lastPresenceMs != 0 &&
                (millis() - _lastPresenceMs) >= AGENT2_OFFLINE_TIMEOUT_MS) {
                _agent2Online = false;
                Serial.println("[FaceGuard] WARNING: Agent 2 presence timeout — marking offline");
                _enqueue({3, false, 0, AlarmDecision::NO_ACTION}, true);
            }
        } else {
            // Update cached broker flag (read by isConnected() on main task)
            portENTER_CRITICAL(&_brokerStateMux);
            bool wasConnected = _brokerConnected;
            _brokerConnected  = false;
            portEXIT_CRITICAL(&_brokerStateMux);

            if (wasConnected) {
                Serial.println("[FaceGuard] MQTT disconnected");
                if (_agent2Online) {
                    _agent2Online = false;
                    _enqueue({3, false, 0, AlarmDecision::NO_ACTION}, true);
                }
            }

            if (millis() - lastReconnectMs >= MQTT_RECONNECT_MS) {
                lastReconnectMs = millis();

                // Pre-resolve hostname to IP outside the mutex.
                // mDNS query uses a FreeRTOS semaphore internally, so this call properly
                // blocks the task (BLOCKED state) and lets IDLE0 run and feed the WDT.
                // Separating DNS from TCP connect ensures connect() never does mDNS,
                // limiting its blocking time to MQTT_SOCKET_TIMEOUT_S seconds.
                if (!_brokerIPResolved) {
                    Serial.printf("[FaceGuard] MQTT resolving %s...\n", _broker);
                    bool resolved = (WiFi.hostByName(_broker, _brokerIP) == 1);
                    if (!resolved) {
                        Serial.printf("[FaceGuard] MQTT DNS failed for %s\n", _broker);
                        continue;
                    }
                    _brokerIPResolved = true;
                    xSemaphoreTake(_mqttMutex, portMAX_DELAY);
                    _mqtt.setServer(_brokerIP, _port);
                    xSemaphoreGive(_mqttMutex);
                }

                Serial.printf("[FaceGuard] MQTT connecting to %s:%u (user=%s)...\n",
                              _broker, _port, _user[0] ? _user : "(none)");

                xSemaphoreTake(_mqttMutex, portMAX_DELAY);
                bool ok = _user[0] ? _mqtt.connect(_clientId, _user, _pass)
                                   : _mqtt.connect(_clientId);
                if (ok) {
                    _mqtt.subscribe(MQTT_TOPIC_PRESENCE);
                    _mqtt.subscribe(MQTT_TOPIC_ALARM);
                    _mqtt.subscribe(MQTT_TOPIC_ALARM_CMD);
                    _mqtt.subscribe(MQTT_TOPIC_DISPLAY_STATUS);
                }
                int rc = ok ? 0 : _mqtt.state();
                xSemaphoreGive(_mqttMutex);

                if (ok) {
                    _connectFailCount = 0;
                    Serial.println("[FaceGuard] MQTT connected");
                    portENTER_CRITICAL(&_brokerStateMux);
                    _brokerConnected = true;
                    portEXIT_CRITICAL(&_brokerStateMux);
                } else {
                    // Force DNS re-resolve after repeated failures in case broker changed IP
                    if (++_connectFailCount >= 5) {
                        _brokerIPResolved = false;
                        _connectFailCount = 0;
                    }
                    Serial.printf("[FaceGuard] MQTT connect failed (rc=%d)\n", rc);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ── Publish helper (called from main task) ────────────────────────────────────

bool AgentComm::_publish(const char* topic, const String& payload) {
    portENTER_CRITICAL(&_brokerStateMux);
    bool connected = _brokerConnected;
    portEXIT_CRITICAL(&_brokerStateMux);
    if (!connected) return false;

    // 100 ms timeout: returns false if MQTT task holds the lock during DNS/connect
    if (xSemaphoreTake(_mqttMutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool r = _mqtt.publish(topic, payload.c_str());
    xSemaphoreGive(_mqttMutex);
    return r;
}

// ── Binary publish for camera frames (called from main task) ─────────────────
//
// Uses PubSubClient beginPublish/write/endPublish streaming path, which bypasses
// the MQTT_MAX_PACKET_SIZE limit that would reject 10-30 KB JPEG payloads.
// Mutex timeout is 50 ms: camera publish is low-priority, so we drop the frame
// rather than stall the main loop while the MQTT task is reconnecting.

bool AgentComm::publishCamera(const uint8_t* buf, size_t len) {
    if (!buf || len == 0 || len > CAMERA_PUB_MAX_BYTES) return false;

    portENTER_CRITICAL(&_brokerStateMux);
    bool connected = _brokerConnected;
    portEXIT_CRITICAL(&_brokerStateMux);
    if (!connected) return false;

    if (xSemaphoreTake(_mqttMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;

    bool ok = _mqtt.beginPublish(MQTT_TOPIC_CAMERA, len, false);
    if (ok) {
        const size_t kChunk = 1024;
        size_t offset = 0;
        while (ok && offset < len) {
            size_t toWrite = min(kChunk, len - offset);
            if (_mqtt.write(buf + offset, toWrite) != toWrite) {
                ok = false;
            } else {
                offset += toWrite;
            }
        }
        if (!_mqtt.endPublish()) ok = false;
    }

    xSemaphoreGive(_mqttMutex);
    return ok;
}

// ── Public API ────────────────────────────────────────────────────────────────

void AgentComm::begin(const char* broker, uint16_t port,
                      const char* username, const char* password,
                      const char* clientId) {
    if (!broker || strlen(broker) == 0) {
        Serial.println("[FaceGuard] MQTT broker not configured — AgentComm disabled");
        return;
    }
    if (_mqttTask != nullptr) return;  // guard against double-init

    strncpy(_broker,   broker,   sizeof(_broker)   - 1);
    strncpy(_clientId, clientId, sizeof(_clientId) - 1);
    strncpy(_user, username ? username : "", sizeof(_user) - 1);
    strncpy(_pass, password ? password : "", sizeof(_pass) - 1);
    _port       = port;
    _configured = true;

    _mqtt.setServer(_broker, _port);
    _mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
    _mqtt.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);
    _mqtt.setCallback(_onMessage);

    _mqttMutex  = xSemaphoreCreateMutex();
    _eventQueue = xQueueCreate(12, sizeof(CommEvent));

    xTaskCreatePinnedToCore(_mqttTaskFn, "mqtt_comm", 8192, nullptr, 1, &_mqttTask, 0);
    Serial.println("[FaceGuard] MQTT task started (Core 0)");
}

void AgentComm::tick() {
    if (!_configured || !_eventQueue) return;

    CommEvent ev;
    while (xQueueReceive(_eventQueue, &ev, 0) == pdTRUE) {
        switch (ev.type) {
            case 1:
                if (_onPresence) _onPresence(ev.boolVal, ev.intVal);
                break;
            case 2:
                if (_onAlarmDecision) _onAlarmDecision(ev.alarm);
                break;
            case 3:
                Serial.printf("[FaceGuard] Agent2 presence %s\n", ev.boolVal ? "online" : "offline");
                if (_onConnChange) _onConnChange(ev.boolVal);
                break;
            case 4:
                if (_onAlarmCommand) _onAlarmCommand(ev.alarm);
                break;
        }
    }
}

bool AgentComm::publishDoor(DoorState state, const char* relatedUser) {
    JsonDocument doc;
    _appendCommonFields(doc);
    doc[MSG_DOOR_STATE] = doorStateToString(state);
    if (relatedUser && relatedUser[0]) doc[MSG_USER_NAME] = relatedUser;
    String payload;
    serializeJson(doc, payload);
    return _publish(MQTT_TOPIC_DOOR, payload);
}

bool AgentComm::publishFace(const char* userName, float similarity) {
    JsonDocument doc;
    _appendCommonFields(doc);
    doc[MSG_USER_NAME]  = userName ? userName : "";
    doc[MSG_SIMILARITY] = similarity;
    String payload;
    serializeJson(doc, payload);
    return _publish(MQTT_TOPIC_FACE, payload);
}

bool AgentComm::publishAlert(AlertLevel level, const char* alertType) {
    JsonDocument doc;
    _appendCommonFields(doc);
    doc[MSG_ALERT_LEVEL] = alertLevelToString(level);
    doc[MSG_ALERT_TYPE]  = alertType ? alertType : "";
    String payload;
    serializeJson(doc, payload);
    return _publish(MQTT_TOPIC_ALERT, payload);
}

bool AgentComm::publishStatus(AlertLevel level, unsigned long uptime) {
    JsonDocument doc;
    _appendCommonFields(doc);
    doc[MSG_ALERT_LEVEL] = alertLevelToString(level);
    doc[MSG_UPTIME]      = uptime;
    String payload;
    serializeJson(doc, payload);
    return _publish(MQTT_TOPIC_STATUS, payload);
}

void AgentComm::setOnPresence(void (*cb)(bool, int))          { _onPresence      = cb; }
void AgentComm::setOnAlarmDecision(void (*cb)(AlarmDecision)) { _onAlarmDecision = cb; }
void AgentComm::setOnAlarmCommand(void (*cb)(AlarmDecision))  { _onAlarmCommand  = cb; }
void AgentComm::setOnConnectionChange(void (*cb)(bool))       { _onConnChange    = cb; }

bool AgentComm::isConnected() {
    portENTER_CRITICAL(&_brokerStateMux);
    bool r = _brokerConnected;
    portEXIT_CRITICAL(&_brokerStateMux);
    return r;
}
