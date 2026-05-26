#pragma once
#include <Arduino.h>
#include "states.h"

// In-memory ring-buffer log for Face, Door, and Alert events.
// Each ring buffer holds up to LOG_CAPACITY entries; oldest entries are
// overwritten when full. Not persisted across reboots (no LittleFS).
//
// Timestamps use time() (NTP); if NTP is not synced the entry includes
// {"time_synced": false} and a millis()-based relative timestamp.

static constexpr uint8_t LOG_CAPACITY = 50;

class LogManager {
public:
  static void begin();

  static void logFace(FaceState state, VoteResult vote,
                      const char* userName, float similarity);

  static void logDoor(DoorState state, const char* relatedUser = nullptr);

  static void logAlert(AlertLevel level, const char* alertType,
                       AlarmDecision decision, bool discordResult);

  // Return JSON arrays (ArduinoJson serialized, newest first).
  // Caller owns the returned String.
  static String getFaceLogJson();
  static String getDoorLogJson();
  static String getAlertLogJson();
};
