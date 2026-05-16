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

  // SHA-256(salt + pw) → 64-char hex string. Public for SessionAuth use.
  static String hashPassword(const String& pw);

private:
  static bool _isValidDiscordUrl(const String& url);
};
