#include "LogManager.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <time.h>

// ── Static member definitions ─────────────────────────────────────────────────

bool     LogManager::_spiffsOk        = false;
uint32_t LogManager::_lastWriteMonth  = 0;
uint8_t  LogManager::_spiffsFailCount = 0;

// ── Timestamp helper ──────────────────────────────────────────────────────────

static bool _ntpSynced() {
  time_t now = time(nullptr);
  return now > 1700000000UL;  // any time after ~Nov 2023 means NTP synced
}

static String _ts() {
  if (!_ntpSynced()) {
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

// ── SPIFFS helpers ────────────────────────────────────────────────────────────

uint32_t LogManager::_currentMonth() {
  if (!_ntpSynced()) return 0;
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  return (uint32_t)(1900 + t->tm_year) * 100u + (uint32_t)(t->tm_mon + 1);
}

uint32_t LogManager::getCurrentMonth() {
  return _currentMonth();
}

uint32_t LogManager::_monthMinus(uint32_t yyyymm, uint8_t n) {
  uint32_t year  = yyyymm / 100u;
  int32_t  month = (int32_t)(yyyymm % 100u) - (int32_t)n;
  while (month <= 0) { year--; month += 12; }
  return year * 100u + (uint32_t)month;
}

void LogManager::_getFilename(char* buf, size_t len,
                               const char* type, uint32_t month, bool meta) {
  snprintf(buf, len, "/%s_%u.%s", type, (unsigned)month, meta ? "meta" : "ndjson");
}

uint32_t LogManager::_readMeta(const char* type, uint32_t month) {
  char metaPath[32];
  _getFilename(metaPath, sizeof(metaPath), type, month, true);
  File f = SPIFFS.open(metaPath, "r");
  if (f) {
    uint32_t count = (uint32_t)f.parseInt();
    f.close();
    if (count > 0) return count;
  }
  // Meta missing or corrupt: count lines in NDJSON file and rebuild meta
  char ndjsonPath[32];
  _getFilename(ndjsonPath, sizeof(ndjsonPath), type, month, false);
  File nf = SPIFFS.open(ndjsonPath, "r");
  if (!nf) return 0;
  uint32_t lines = 0;
  while (nf.available()) { if (nf.read() == '\n') lines++; }
  nf.close();
  if (lines > 0) _writeMeta(type, month, lines);
  return lines;
}

void LogManager::_writeMeta(const char* type, uint32_t month, uint32_t count) {
  char path[32];
  _getFilename(path, sizeof(path), type, month, true);
  File f = SPIFFS.open(path, "w");
  if (!f) return;
  f.print(count);
  f.close();
}

void LogManager::_rotateOldFiles() {
  uint32_t m = _currentMonth();
  if (m == 0) return;
  uint32_t cutoff = _monthMinus(m, 1);  // keep current month + 1 previous month

  // Collect candidates first to avoid iterator issues during deletion
  char victims[12][36];
  int victimCount = 0;

  File root = SPIFFS.open("/");
  if (!root) return;

  File file = root.openNextFile();
  while (file && victimCount < 12) {
    const char* fname = file.name();
    // fname is "/type_YYYYMM.ext" on ESP32 SPIFFS
    const char* base = strrchr(fname, '/');
    base = base ? base + 1 : fname;

    const char* us  = strrchr(base, '_');
    const char* dot = strrchr(base, '.');
    if (us && dot && dot > us + 1 && (dot - us - 1) == 6) {
      char mstr[7] = {};
      strncpy(mstr, us + 1, 6);
      uint32_t fm = (uint32_t)atoi(mstr);
      if (fm > 200000u && fm < cutoff) {
        // Build full path with leading /
        char fullPath[36];
        if (fname[0] == '/') {
          strncpy(fullPath, fname, sizeof(fullPath) - 1);
        } else {
          fullPath[0] = '/';
          strncpy(fullPath + 1, fname, sizeof(fullPath) - 2);
        }
        fullPath[sizeof(fullPath) - 1] = '\0';
        strncpy(victims[victimCount++], fullPath, sizeof(victims[0]) - 1);
      }
    }
    file = root.openNextFile();
  }
  root.close();

  for (int i = 0; i < victimCount; i++) {
    SPIFFS.remove(victims[i]);
    Serial.printf("[LogManager] rotated: %s\n", victims[i]);
  }
}

void LogManager::_appendToSpiffs(const char* logType, const char* jsonLine) {
  uint32_t m = _currentMonth();
  if (m == 0) return;  // NTP not synced

  if (m != _lastWriteMonth) {
    _rotateOldFiles();
  }
  _lastWriteMonth = m;

  char path[32];
  _getFilename(path, sizeof(path), logType, m, false);

  File f = SPIFFS.open(path, "a");
  if (!f) {
    if (++_spiffsFailCount >= 10) {
      _spiffsOk = false;
      Serial.println("[LogManager] SPIFFS disabled after 10 consecutive failures");
    }
    return;
  }
  f.println(jsonLine);
  f.close();
  _spiffsFailCount = 0;

  uint32_t cnt = _readMeta(logType, m) + 1;
  _writeMeta(logType, m, cnt);
}

// ── Public lifecycle ──────────────────────────────────────────────────────────

void LogManager::begin() {
  memset(_faceLog,  0, sizeof(_faceLog));
  memset(_doorLog,  0, sizeof(_doorLog));
  memset(_alertLog, 0, sizeof(_alertLog));
  _faceHead  = _faceCount  = 0;
  _doorHead  = _doorCount  = 0;
  _alertHead = _alertCount = 0;
}

bool LogManager::beginSpiffs() {
  Serial.println("[LogManager] mounting SPIFFS (first-time format may take a few seconds)...");
  if (!SPIFFS.begin(true)) {
    Serial.println("[LogManager] SPIFFS unavailable — RAM-only mode");
    _spiffsOk = false;
    return false;
  }
  _spiffsOk = true;
  Serial.printf("[LogManager] SPIFFS mounted (total:%uKB free:%uKB)\n",
                (unsigned)(SPIFFS.totalBytes() / 1024u),
                (unsigned)((SPIFFS.totalBytes() - SPIFFS.usedBytes()) / 1024u));
  _rotateOldFiles();
  return true;
}

bool LogManager::isSpiffsAvailable() {
  return _spiffsOk;
}

// ── Log writers ───────────────────────────────────────────────────────────────

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

  if (_spiffsOk && _ntpSynced()) {
    JsonDocument ldoc;
    ldoc["timestamp"]   = e.timestamp;
    ldoc["face_state"]  = faceStateToString(e.faceState);
    ldoc["vote_result"] = voteResultToString(e.voteResult);
    ldoc["user_name"]   = e.userName;
    ldoc["similarity"]  = e.similarity;
    String line;
    serializeJson(ldoc, line);
    _appendToSpiffs("face", line.c_str());
  }
}

void LogManager::logDoor(DoorState state, const char* relatedUser) {
  DoorEntry& e = _doorLog[_doorHead];
  strncpy(e.timestamp, _ts().c_str(), sizeof(e.timestamp) - 1);
  e.doorState = state;
  strncpy(e.relatedUser, relatedUser ? relatedUser : "", sizeof(e.relatedUser) - 1);
  _doorHead = (_doorHead + 1) % LOG_CAPACITY;
  if (_doorCount < LOG_CAPACITY) _doorCount++;

  if (_spiffsOk && _ntpSynced()) {
    JsonDocument ldoc;
    ldoc["timestamp"]    = e.timestamp;
    ldoc["door_state"]   = doorStateToString(e.doorState);
    ldoc["related_user"] = e.relatedUser;
    String line;
    serializeJson(ldoc, line);
    _appendToSpiffs("door", line.c_str());
  }
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

  if (_spiffsOk && _ntpSynced()) {
    JsonDocument ldoc;
    ldoc["timestamp"]      = e.timestamp;
    ldoc["alert_level"]    = alertLevelToString(e.alertLevel);
    ldoc["alert_type"]     = e.alertType;
    ldoc["alarm_decision"] = (e.alarmDecision == AlarmDecision::TRIGGER_ALARM ? "TRIGGER_ALARM" :
                              e.alarmDecision == AlarmDecision::CANCEL_ALARM  ? "CANCEL_ALARM"  :
                                                                               "NO_ACTION");
    ldoc["discord_result"] = e.discordResult;
    String line;
    serializeJson(ldoc, line);
    _appendToSpiffs("alert", line.c_str());
  }
}

// ── Ring buffer serialization ─────────────────────────────────────────────────

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

// ── SPIFFS paged queries ──────────────────────────────────────────────────────

String LogManager::_getPagedJson(const char* logType,
                                  uint32_t month, uint16_t page, uint16_t perPage) {
  uint32_t total = _spiffsOk ? _readMeta(logType, month) : 0;

  JsonDocument doc;
  doc["total"]    = total;
  doc["page"]     = page;
  doc["per_page"] = perPage;
  JsonArray data  = doc["data"].to<JsonArray>();

  if (total == 0 || !_spiffsOk) {
    String out;
    serializeJson(doc, out);
    return out;
  }

  char path[32];
  _getFilename(path, sizeof(path), logType, month, false);
  File f = SPIFFS.open(path, "r");
  if (!f) {
    String out;
    serializeJson(doc, out);
    return out;
  }

  uint32_t skip  = (uint32_t)(page - 1) * perPage;
  uint32_t count = 0;
  char lineBuf[256];

  while (f.available()) {
    int len = 0;
    bool overflow = false;
    while (f.available()) {
      char c = (char)f.read();
      if (c == '\n' || c == '\r') break;
      if (len < (int)sizeof(lineBuf) - 1) { lineBuf[len++] = c; }
      else { overflow = true; }
    }
    lineBuf[len] = '\0';
    if (len < 10 || overflow) continue;

    if (skip > 0) { skip--; continue; }

    {
      JsonDocument ldoc;
      if (!deserializeJson(ldoc, lineBuf)) {
        data.add(ldoc.as<JsonObject>());
      }
    }
    if (++count >= perPage) break;
  }
  f.close();

  String out;
  serializeJson(doc, out);
  return out;
}

String LogManager::getDoorLogPagedJson(uint32_t month, uint16_t page, uint16_t perPage) {
  return _getPagedJson("door", month, page, perPage);
}

String LogManager::getFaceLogPagedJson(uint32_t month, uint16_t page, uint16_t perPage) {
  return _getPagedJson("face", month, page, perPage);
}

String LogManager::getAlertLogPagedJson(uint32_t month, uint16_t page, uint16_t perPage) {
  return _getPagedJson("alert", month, page, perPage);
}

// ── Statistics ────────────────────────────────────────────────────────────────

String LogManager::getStatsJson(uint32_t month) {
  if (month == 0) month = _currentMonth();
  // Reject obviously invalid YYYYMM values to prevent mktime computing wrong dates
  if (month != 0) {
    uint32_t mon = month % 100u;
    if (mon < 1 || mon > 12) {
      JsonDocument edoc; edoc["month"] = month;
      String eout; serializeJson(edoc, eout); return eout;
    }
  }

  char todayStr[11]     = {};
  char weekDays[7][11]  = {};
  char weekLabels[7][3] = {};
  // Explicit historical month: refTime is derived from month param alone, NTP not required.
  // Only month==0 (current month) requires NTP to know which month we're in.
  bool hasTime = _ntpSynced() || (month != 0);
  static const char* DOW_ABBR[] = { "S","M","T","W","T","F","S" };

  if (hasTime) {
    time_t refTime;
    uint32_t curMonth = _currentMonth();
    if (month == curMonth) {
      refTime = time(nullptr);
    } else {
      // Historical: use the last day of the requested month as reference
      uint32_t year = month / 100u;
      uint32_t mon  = month % 100u;
      struct tm lastDayTm = {};
      lastDayTm.tm_year  = (int)(year - 1900);
      lastDayTm.tm_mon   = (int)mon;  // one past requested month (0-based); mday=0 = last day
      lastDayTm.tm_mday  = 0;
      lastDayTm.tm_isdst = -1;
      refTime = mktime(&lastDayTm);
    }
    struct tm* refTm = localtime(&refTime);
    strftime(todayStr, sizeof(todayStr), "%Y-%m-%d", refTm);
    for (int i = 0; i < 7; i++) {
      time_t d = refTime - (time_t)(6 - i) * 86400L;
      struct tm* dt = localtime(&d);
      strftime(weekDays[i], sizeof(weekDays[i]), "%Y-%m-%d", dt);
      weekLabels[i][0] = DOW_ABBR[dt->tm_wday][0];
      weekLabels[i][1] = '\0';
    }
  }

  JsonDocument doc;
  doc["month"] = month;
  if (hasTime) {
    JsonArray wl = doc["week_labels"].to<JsonArray>();
    for (int i = 0; i < 7; i++) wl.add(weekLabels[i]);
  }

  static const char* types[] = { "door", "face", "alert" };
  for (int ti = 0; ti < 3; ti++) {
    const char* type   = types[ti];
    uint32_t metaTotal = _spiffsOk ? _readMeta(type, month) : 0;

    JsonObject tobj = doc[type].to<JsonObject>();
    tobj["month_total"] = metaTotal;
    tobj["today"]       = 0;
    tobj["week"]        = 0;
    JsonArray daily     = tobj["daily_week"].to<JsonArray>();
    for (int i = 0; i < 7; i++) daily.add(0);

    if (ti == 0) { tobj["open_count"] = 0; tobj["close_count"] = 0; }
    if (ti == 1) { tobj["known_count"] = 0; tobj["known_pct"] = 0; }
    if (ti == 2) { tobj["red_count"] = 0; tobj["yellow_count"] = 0; tobj["last_at"] = ""; }

    if (!_spiffsOk || !hasTime || metaTotal == 0) continue;

    char path[32];
    _getFilename(path, sizeof(path), type, month, false);
    File f = SPIFFS.open(path, "r");
    if (!f) continue;

    char lineBuf[256];
    while (f.available()) {
      int len = 0;
      bool overflow = false;
      while (f.available()) {
        char c = (char)f.read();
        if (c == '\n' || c == '\r') break;
        if (len < (int)sizeof(lineBuf) - 1) { lineBuf[len++] = c; }
        else { overflow = true; }
      }
      lineBuf[len] = '\0';
      if (len < 15 || overflow) continue;

      // Extract date from {"timestamp":"YYYY-MM-DD..."}
      const char* ts = strstr(lineBuf, "\"timestamp\":\"");
      if (!ts) continue;
      ts += 13;  // skip past "timestamp":"
      char date[11] = {};
      strncpy(date, ts, 10);

      if (strcmp(date, todayStr) == 0) {
        tobj["today"] = (int)tobj["today"] + 1;
      }
      for (int i = 0; i < 7; i++) {
        if (strcmp(date, weekDays[i]) == 0) {
          daily[i] = (int)daily[i] + 1;
          break;
        }
      }

      if (ti == 0) {
        if (strstr(lineBuf, "\"DOOR_OPEN\""))
          tobj["open_count"] = (int)tobj["open_count"] + 1;
        if (strstr(lineBuf, "\"DOOR_CLOSED\""))
          tobj["close_count"] = (int)tobj["close_count"] + 1;
      } else if (ti == 1) {
        if (strstr(lineBuf, "\"KNOWN_CONFIRMED\""))
          tobj["known_count"] = (int)tobj["known_count"] + 1;
      } else {
        if (strstr(lineBuf, "\"ALERT_RED\""))
          tobj["red_count"] = (int)tobj["red_count"] + 1;
        if (strstr(lineBuf, "\"ALERT_YELLOW\""))
          tobj["yellow_count"] = (int)tobj["yellow_count"] + 1;
        char tsVal[32] = {};
        strncpy(tsVal, ts, 25);
        tobj["last_at"] = tsVal;
      }
    }
    f.close();

    int weekTotal = 0;
    for (int i = 0; i < 7; i++) weekTotal += (int)daily[i];
    tobj["week"] = weekTotal;

    if (ti == 1 && metaTotal > 0) {
      tobj["known_pct"] = (int)((float)(int)tobj["known_count"] / (float)metaTotal * 100.0f);
    }
  }

  String out;
  serializeJson(doc, out);
  return out;
}

String LogManager::getAvailableMonthsJson() {
  if (!_spiffsOk) return "[]";

  uint32_t months[12] = {};
  int count = 0;

  File root = SPIFFS.open("/");
  if (!root) return "[]";

  File file = root.openNextFile();
  while (file && count < 12) {
    const char* fname = file.name();
    const char* base  = strrchr(fname, '/');
    base = base ? base + 1 : fname;

    const char* us  = strrchr(base, '_');
    const char* dot = strrchr(base, '.');
    // Only count .ndjson files; skip .meta
    if (us && dot && dot > us + 1 && (dot - us - 1) == 6 &&
        strcmp(dot, ".ndjson") == 0) {
      char mstr[7] = {};
      strncpy(mstr, us + 1, 6);
      uint32_t m = (uint32_t)atoi(mstr);
      if (m > 200000u) {
        bool found = false;
        for (int i = 0; i < count; i++) {
          if (months[i] == m) { found = true; break; }
        }
        if (!found) months[count++] = m;
      }
    }
    file = root.openNextFile();
  }
  root.close();

  // Insertion sort descending
  for (int i = 1; i < count; i++) {
    uint32_t key = months[i];
    int j = i - 1;
    while (j >= 0 && months[j] < key) { months[j + 1] = months[j]; j--; }
    months[j + 1] = key;
  }

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < count; i++) arr.add(months[i]);

  String out;
  serializeJson(doc, out);
  return out;
}
