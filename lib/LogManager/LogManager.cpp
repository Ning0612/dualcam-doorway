#include "LogManager.h"
#include <ArduinoJson.h>
#include <time.h>

// ── Timestamp helper ──────────────────────────────────────────────────────────

static bool _ntpSynced() {
  time_t now = time(nullptr);
  return now > 1700000000UL;  // any time after ~Nov 2023 means NTP synced
}

static String _ts() {
  if (!_ntpSynced()) {
    // Return relative ms when NTP not available
    char buf[24];
    snprintf(buf, sizeof(buf), "rel:%lu", millis());
    return String(buf);
  }
  char buf[32];
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S+08:00", t);
  return String(buf);
}

// ── Ring buffer ───────────────────────────────────────────────────────────────

struct FaceEntry {
  char          timestamp[32];
  FaceState     faceState;
  VoteResult    voteResult;
  char          userName[17];
  float         similarity;
};

struct DoorEntry {
  char          timestamp[32];
  DoorState     doorState;
  char          relatedUser[17];
};

struct AlertEntry {
  char          timestamp[32];
  AlertLevel    alertLevel;
  char          alertType[24];
  AlarmDecision alarmDecision;
  bool          discordResult;
};

static FaceEntry  _faceLog[LOG_CAPACITY];
static DoorEntry  _doorLog[LOG_CAPACITY];
static AlertEntry _alertLog[LOG_CAPACITY];

static uint8_t _faceHead  = 0, _faceCount  = 0;
static uint8_t _doorHead  = 0, _doorCount  = 0;
static uint8_t _alertHead = 0, _alertCount = 0;

// ── Public API ────────────────────────────────────────────────────────────────

void LogManager::begin() {
  memset(_faceLog,  0, sizeof(_faceLog));
  memset(_doorLog,  0, sizeof(_doorLog));
  memset(_alertLog, 0, sizeof(_alertLog));
  _faceHead  = _faceCount  = 0;
  _doorHead  = _doorCount  = 0;
  _alertHead = _alertCount = 0;
}

void LogManager::logFace(FaceState state, VoteResult vote,
                          const char* userName, float similarity) {
  FaceEntry& e = _faceLog[_faceHead];
  strncpy(e.timestamp, _ts().c_str(), sizeof(e.timestamp) - 1);
  e.faceState  = state;
  e.voteResult = vote;
  strncpy(e.userName, userName ? userName : "", sizeof(e.userName) - 1);
  e.similarity = similarity;
  _faceHead = (_faceHead + 1) % LOG_CAPACITY;
  if (_faceCount < LOG_CAPACITY) _faceCount++;
}

void LogManager::logDoor(DoorState state, const char* relatedUser) {
  DoorEntry& e = _doorLog[_doorHead];
  strncpy(e.timestamp, _ts().c_str(), sizeof(e.timestamp) - 1);
  e.doorState = state;
  strncpy(e.relatedUser, relatedUser ? relatedUser : "", sizeof(e.relatedUser) - 1);
  _doorHead = (_doorHead + 1) % LOG_CAPACITY;
  if (_doorCount < LOG_CAPACITY) _doorCount++;
}

void LogManager::logAlert(AlertLevel level, const char* alertType,
                           AlarmDecision decision, bool discordResult) {
  AlertEntry& e = _alertLog[_alertHead];
  strncpy(e.timestamp, _ts().c_str(), sizeof(e.timestamp) - 1);
  e.alertLevel    = level;
  strncpy(e.alertType, alertType ? alertType : "", sizeof(e.alertType) - 1);
  e.alarmDecision = decision;
  e.discordResult = discordResult;
  _alertHead = (_alertHead + 1) % LOG_CAPACITY;
  if (_alertCount < LOG_CAPACITY) _alertCount++;
}

// Serialise a ring buffer newest-first.
// The ring buffer writes head forward; to read newest first we walk backward
// from head-1 for count entries.

String LogManager::getFaceLogJson() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < _faceCount; i++) {
    int idx = (int)(_faceHead - 1 - i + LOG_CAPACITY) % LOG_CAPACITY;
    const FaceEntry& e = _faceLog[idx];
    JsonObject obj = arr.add<JsonObject>();
    obj["timestamp"]   = e.timestamp;
    obj["face_state"]  = faceStateToString(e.faceState);
    obj["vote_result"] = voteResultToString(e.voteResult);
    obj["user_name"]   = e.userName;
    obj["similarity"]  = e.similarity;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

String LogManager::getDoorLogJson() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < _doorCount; i++) {
    int idx = (int)(_doorHead - 1 - i + LOG_CAPACITY) % LOG_CAPACITY;
    const DoorEntry& e = _doorLog[idx];
    JsonObject obj = arr.add<JsonObject>();
    obj["timestamp"]    = e.timestamp;
    obj["door_state"]   = doorStateToString(e.doorState);
    obj["related_user"] = e.relatedUser;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

String LogManager::getAlertLogJson() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < _alertCount; i++) {
    int idx = (int)(_alertHead - 1 - i + LOG_CAPACITY) % LOG_CAPACITY;
    const AlertEntry& e = _alertLog[idx];
    JsonObject obj = arr.add<JsonObject>();
    obj["timestamp"]      = e.timestamp;
    obj["alert_level"]    = alertLevelToString(e.alertLevel);
    obj["alert_type"]     = e.alertType;
    obj["alarm_decision"] = (e.alarmDecision == AlarmDecision::TRIGGER_ALARM ? "TRIGGER_ALARM" :
                             e.alarmDecision == AlarmDecision::CANCEL_ALARM  ? "CANCEL_ALARM"  :
                                                                               "NO_ACTION");
    obj["discord_result"] = e.discordResult;
  }
  String out;
  serializeJson(doc, out);
  return out;
}
