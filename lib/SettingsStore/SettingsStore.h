#pragma once
#include <Arduino.h>

class SettingsStore {
public:
  static void init();

  // Returns true if the admin password has never been changed from the default.
  static bool hasDefaultPassword();

  // Returns the stored SHA-256 hex hash, or "" if not set.
  static String getDashboardPwHash();

  // Validates length (8-64), hashes, and stores. Returns false on invalid input.
  static bool setDashboardPassword(const String& newPassword);

  // Returns stored Discord webhook URL, or "".
  static String getDiscordUrl();

  // Validates URL prefix and length, then stores. Pass "" to clear.
  static bool setDiscordUrl(const String& url);

  // Hall-effect door sensor open-zone bounds. Range 0-4095 (12-bit ADC).
  // Door OPEN when lowerBound < raw < upperBound. Returns defaults if not set.
  static uint16_t getHallLowerBound();
  static uint16_t getHallUpperBound();
  // Validates: lower < upper and upper - lower > 2 * HALL_HYSTERESIS.
  static bool     setHallBounds(uint16_t lower, uint16_t upper);

  // SHA-256(salt + pw) → 64-char hex string. Public for SessionAuth use.
  static String hashPassword(const String& pw);

  // Timezone offset from UTC in minutes. Range: -720 to +840. Default 480 (UTC+8).
  static int16_t getTzOffset();
  static bool    setTzOffset(int16_t offsetMin);   // validates, saves, applies immediately
  static void    applyTzToSystem(int16_t offsetMin); // builds POSIX TZ string, setenv + tzset

private:
  static bool _isValidDiscordUrl(const String& url);
};
