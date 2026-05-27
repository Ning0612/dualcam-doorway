#pragma once
#include <Arduino.h>
#include "states.h"

// Compile-time TLS flags (define in platformio.ini build_flags):
//   DISCORD_TLS_INSECURE   — skip TLS verification; development/testing only
//   DISCORD_ROOT_CA_CERT   — define as a PEM string to enable CA verification
// If neither is defined the build will fail with a descriptive error.

class DiscordNotifier {
public:
  // Send a Discord webhook notification.
  // event: the AlertEvent being reported (used for per-event rate limiting).
  // Returns true on success; false on rate-limit, cooldown, invalid URL, or error.
  static bool notify(const String& webhookUrl, AlertEvent event,
                     const String& message);

  // Send a Discord webhook notification with a JPEG photo attachment.
  // Uses multipart/form-data. Falls back to text-only notify() if jpegBuf is null.
  // jpegBuf ownership stays with the caller (not freed here).
  static bool notifyWithPhoto(const String& webhookUrl, AlertEvent event,
                               const String& message,
                               const uint8_t* jpegBuf, size_t jpegLen);

  // One-shot startup notification (e.g., boot IP announcement).
  // Intentionally does NOT update _failCooldownUntil or _lastNotifyMs so that
  // a failed boot message cannot suppress subsequent security-alert notifications.
  static bool notifyBoot(const String& webhookUrl, const String& message);

private:
  // Array size must equal the number of AlertEvent values.
  // If a new AlertEvent is added, increase this constant and update the array.
  static constexpr int ALERT_EVENT_COUNT = 3;  // UNKNOWN_VISITOR, USER_RETURNED, BOOT
  static_assert(static_cast<int>(AlertEvent::BOOT) + 1 == ALERT_EVENT_COUNT,
                "DiscordNotifier: _lastNotifyMs size out of sync with AlertEvent enum");

  static unsigned long _lastNotifyMs[ALERT_EVENT_COUNT];
  static unsigned long _failCooldownStartMs;  // 0 = not in cooldown; rollover-safe via elapsed check

  static bool _isValidUrl(const String& url);

  // Fail-fast guard for notify() and notifyWithPhoto().
  // Checks WiFi, URL validity, fail cooldown, and per-event rate limit.
  // notifyBoot() intentionally skips this to avoid polluting security-alert throttling.
  static bool _canSend(const String& url, AlertEvent event);
};
