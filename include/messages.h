#pragma once

// ── MQTT topics (Agent 1 publish) ────────────────────────────────────────────
#define MQTT_TOPIC_DOOR     "home/security/door"
#define MQTT_TOPIC_FACE     "home/security/face"
#define MQTT_TOPIC_ALERT    "home/security/alert"
#define MQTT_TOPIC_STATUS   "home/security/status"

// ── MQTT topics (Agent 1 subscribe) ─────────────────────────────────────────
#define MQTT_TOPIC_PRESENCE "home/home_state/presence"
#define MQTT_TOPIC_ALARM    "home/home_state/alarm_decision"

// ── JSON field keys (MQTT payloads) ──────────────────────────────────────────
#define MSG_TIMESTAMP       "timestamp"
#define MSG_DOOR_STATE      "door_state"
#define MSG_FACE_STATE      "face_state"
#define MSG_ALERT_LEVEL     "alert_level"
#define MSG_USER_NAME       "user_name"
#define MSG_SIMILARITY      "similarity"
#define MSG_ALARM_DECISION  "alarm_decision"
#define MSG_PRESENCE_STATE  "presence_state"
#define MSG_PRESENCE_SCORE  "presence_score"
#define MSG_ALERT_TYPE      "alert_type"
#define MSG_UPTIME          "uptime"

// ── Presence state values ─────────────────────────────────────────────────────
#define PRESENCE_OCCUPIED   "OCCUPIED"
#define PRESENCE_UNOCCUPIED "UNOCCUPIED"

// ── Alarm decision values ─────────────────────────────────────────────────────
#define ALARM_TRIGGER       "TRIGGER_ALARM"
#define ALARM_CANCEL        "CANCEL_ALARM"
#define ALARM_NONE          "NO_ACTION"
