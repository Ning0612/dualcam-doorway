#pragma once
#include <Arduino.h>
#include "states.h"

// Compile-time TLS flags (define in platformio.ini build_flags):
//   DISCORD_TLS_INSECURE   — skip TLS verification; development/testing only, NOT for production
//   DISCORD_ROOT_CA_CERT   — define as a PEM string to enable CA verification
// If neither is defined the build will fail with a descriptive error.

class DiscordNotifier {
public:
  // Send a Discord webhook notification.
  // forState: the SystemState being reported (used for per-state rate limiting).
  // Returns true on success; false on rate-limit, cooldown, invalid URL, or error.
  static bool notify(const String& webhookUrl, SystemState forState,
                     const String& message);

private:
  static unsigned long _lastNotifyMs[9];   // indexed by (int)SystemState
  static unsigned long _failCooldownUntil;

  static bool _isValidUrl(const String& url);
};
