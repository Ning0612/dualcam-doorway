#include "AgentComm.h"
#include "config.h"
#include "messages.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

// ── Static storage ────────────────────────────────────────────────────────────

static WiFiClient   _wifiClient;
static PubSubClient _mqtt(_wifiClient);

static char     _broker[64]   = {};
static uint16_t _port         = MQTT_DEFAULT_PORT;
static char     _clientId[24] = "agent1";
static bool     _configured   = false;

static unsigned long _lastReconnectMs = 0;
static bool _brokerConnected          = false;

// Agent 2 online state is tracked via presence heartbeat freshness,
// NOT by MQTT broker connection. Broker online ≠ Agent 2 online.
static bool          _agent2Online    = false;
static unsigned long _lastPresenceMs  = 0;

static void (*_onPresence)(bool, int)       = nullptr;
static void (*_onAlarmDecision)(AlarmDecision) = nullptr;
static void (*_onConnChange)(bool)          = nullptr;

// ── MQTT message handler ──────────────────────────────────────────────────────

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

    // Update Agent 2 freshness and fire connection change if state flipped
    _lastPresenceMs = millis();
    if (!_agent2Online) {
      _agent2Online = true;
      if (_onConnChange) _onConnChange(true);
    }

    if (_onPresence) _onPresence(occupied, score);

  } else if (strcmp(topic, MQTT_TOPIC_ALARM) == 0) {
    if (!_onAlarmDecision) return;
    const char* ad = doc[MSG_ALARM_DECISION] | "";
    AlarmDecision decision = AlarmDecision::NO_ACTION;
    if      (strcmp(ad, ALARM_TRIGGER) == 0) decision = AlarmDecision::TRIGGER_ALARM;
    else if (strcmp(ad, ALARM_CANCEL)  == 0) decision = AlarmDecision::CANCEL_ALARM;
    _onAlarmDecision(decision);
  }
}

// ── Reconnect ─────────────────────────────────────────────────────────────────

void AgentComm::_reconnect() {
  if (!_configured || WiFi.status() != WL_CONNECTED) return;
  if (millis() - _lastReconnectMs < MQTT_RECONNECT_MS) return;
  _lastReconnectMs = millis();

  Serial.printf("[Agent1] MQTT connecting to %s:%u...\n", _broker, _port);
  if (_mqtt.connect(_clientId)) {
    Serial.println("[Agent1] MQTT connected");
    _mqtt.subscribe(MQTT_TOPIC_PRESENCE);
    _mqtt.subscribe(MQTT_TOPIC_ALARM);
    _brokerConnected = true;
    // Agent 2 online state is determined by presence heartbeats, not broker connect.
    // _onConnChange is NOT fired here; wait for the first presence message.
  } else {
    Serial.printf("[Agent1] MQTT connect failed (rc=%d)\n", _mqtt.state());
  }
}

// ── Publish helper ────────────────────────────────────────────────────────────

bool AgentComm::_publish(const char* topic, const String& payload) {
  if (!_mqtt.connected()) return false;
  return _mqtt.publish(topic, payload.c_str());
}

// ── Public API ────────────────────────────────────────────────────────────────

void AgentComm::begin(const char* broker, uint16_t port, const char* clientId) {
  if (!broker || strlen(broker) == 0) {
    Serial.println("[Agent1] MQTT broker not configured — AgentComm disabled");
    return;
  }
  strncpy(_broker, broker, sizeof(_broker) - 1);
  strncpy(_clientId, clientId, sizeof(_clientId) - 1);
  _port       = port;
  _configured = true;

  _mqtt.setServer(_broker, _port);
  _mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
  _mqtt.setCallback(_onMessage);
}

void AgentComm::tick() {
  if (!_configured) return;

  if (_mqtt.connected()) {
    _mqtt.loop();
    _brokerConnected = true;

    // Agent 2 presence timeout: broker connected but no heartbeat within window
    if (_agent2Online && _lastPresenceMs != 0 &&
        (millis() - _lastPresenceMs) >= AGENT2_OFFLINE_TIMEOUT_MS) {
      _agent2Online = false;
      Serial.println("[Agent1] WARNING: Agent 2 presence timeout — marking offline");
      if (_onConnChange) _onConnChange(false);
    }
  } else {
    if (_brokerConnected) {
      _brokerConnected = false;
      Serial.println("[Agent1] MQTT disconnected");
      // Broker disconnect means Agent 2 is unreachable
      if (_agent2Online) {
        _agent2Online = false;
        if (_onConnChange) _onConnChange(false);
      }
    }
    _reconnect();
  }
}

bool AgentComm::publishDoor(DoorState state, const char* relatedUser) {
  JsonDocument doc;
  doc[MSG_DOOR_STATE] = doorStateToString(state);
  if (relatedUser && relatedUser[0]) doc[MSG_USER_NAME] = relatedUser;
  doc[MSG_TIMESTAMP]  = millis();
  String payload;
  serializeJson(doc, payload);
  return _publish(MQTT_TOPIC_DOOR, payload);
}

bool AgentComm::publishFace(const char* userName, float similarity) {
  JsonDocument doc;
  doc[MSG_USER_NAME]  = userName ? userName : "";
  doc[MSG_SIMILARITY] = similarity;
  doc[MSG_TIMESTAMP]  = millis();
  String payload;
  serializeJson(doc, payload);
  return _publish(MQTT_TOPIC_FACE, payload);
}

bool AgentComm::publishAlert(AlertLevel level, const char* alertType) {
  JsonDocument doc;
  doc[MSG_ALERT_LEVEL] = alertLevelToString(level);
  doc[MSG_ALERT_TYPE]  = alertType ? alertType : "";
  doc[MSG_TIMESTAMP]   = millis();
  String payload;
  serializeJson(doc, payload);
  return _publish(MQTT_TOPIC_ALERT, payload);
}

bool AgentComm::publishStatus(AlertLevel level, unsigned long uptime) {
  JsonDocument doc;
  doc[MSG_ALERT_LEVEL] = alertLevelToString(level);
  doc[MSG_UPTIME]      = uptime;
  doc[MSG_TIMESTAMP]   = millis();
  String payload;
  serializeJson(doc, payload);
  return _publish(MQTT_TOPIC_STATUS, payload);
}

void AgentComm::setOnPresence(void (*cb)(bool, int))          { _onPresence      = cb; }
void AgentComm::setOnAlarmDecision(void (*cb)(AlarmDecision)) { _onAlarmDecision = cb; }
void AgentComm::setOnConnectionChange(void (*cb)(bool))       { _onConnChange    = cb; }
bool AgentComm::isConnected()                                 { return _mqtt.connected(); }
