#pragma once
#include <Arduino.h>

class ConfigPortal {
public:
  // Blocks until WiFi STA is connected via DHCP.
  // If NVS has no credentials, or connect fails after WIFI_CONNECT_TIMEOUT_MS,
  // opens AP mode and serves a config page.
  // After POST /save the device restarts; after restart this call returns normally.
  static void begin(const char* apName);

  // Erase wifi_ssid and wifi_pw from NVS. Returns true on success.
  // Call before ESP.restart() to force the next boot into AP portal mode.
  static bool clearCredentials();

private:
  static bool _tryConnect(const String& ssid, const String& pw);
  static void _runPortal(const char* apName);
};
