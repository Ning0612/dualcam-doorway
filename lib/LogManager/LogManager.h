#pragma once
#include <Arduino.h>
#include "states.h"

// In-memory ring-buffer log for Face, Door, and Alert events.
// Each ring buffer holds up to LOG_CAPACITY entries; oldest entries are
// overwritten when full. Persistent history is stored on SPIFFS as monthly
// NDJSON files; call beginSpiffs() once after begin() to enable persistence.
//
// Timestamps use time() (NTP); if NTP is not synced the entry uses
// a millis()-based relative timestamp. SPIFFS writes are silently skipped
// when NTP is not synced.

static constexpr uint8_t LOG_CAPACITY = 50;

class LogManager {
public:
  static void begin();

  // Mount SPIFFS and enable persistent log storage.
  // Returns true on success; on failure falls back to RAM-only mode.
  static bool beginSpiffs();
  static bool isSpiffsAvailable();

  // Current month as YYYYMM (e.g. 202505). Returns 0 if NTP not synced.
  static uint32_t getCurrentMonth();

  static void logFace(FaceState state, VoteResult vote,
                      const char* userName, float similarity);

  static void logDoor(DoorState state, const char* relatedUser = nullptr);

  static void logAlert(AlertLevel level, const char* alertType,
                       AlarmDecision decision, bool discordResult);

  // JSON arrays from in-memory ring buffer only (newest first).
  static String getFaceLogJson();
  static String getDoorLogJson();
  static String getAlertLogJson();

  // Paged SPIFFS history. page is 1-based.
  // Returns {"total":N,"page":P,"per_page":PP,"data":[...]}.
  static String getDoorLogPagedJson(uint32_t month, uint16_t page, uint16_t perPage = 20);
  static String getFaceLogPagedJson(uint32_t month, uint16_t page, uint16_t perPage = 20);
  static String getAlertLogPagedJson(uint32_t month, uint16_t page, uint16_t perPage = 20);

  // Analytics for the given month (0 = current month).
  // Returns today/week/month_total counts, daily_week[7], and type-specific fields.
  static String getStatsJson(uint32_t month = 0);

  // Months that have log data, descending: [202505, 202504, ...].
  static String getAvailableMonthsJson();

private:
  static bool     _spiffsOk;
  static uint32_t _lastWriteMonth;
  static uint8_t  _spiffsFailCount;

  static void     _appendToSpiffs(const char* logType, const char* jsonLine);
  static void     _rotateOldFiles();
  static void     _getFilename(char* buf, size_t len, const char* type, uint32_t month, bool meta);
  static uint32_t _currentMonth();
  static uint32_t _monthMinus(uint32_t yyyymm, uint8_t n);
  static uint32_t _readMeta(const char* type, uint32_t month);
  static void     _writeMeta(const char* type, uint32_t month, uint32_t count);
  static String   _getPagedJson(const char* logType, uint32_t month, uint16_t page, uint16_t perPage);
};
