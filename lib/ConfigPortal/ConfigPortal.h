#pragma once
#include <Arduino.h>
#include <IPAddress.h>

class ConfigPortal {
public:
  // Blocks until WiFi STA is connected with the given static IP.
  // If NVS has no credentials, or connect fails after WIFI_CONNECT_TIMEOUT_MS,
  // opens AP mode and serves a config page.
  // After POST /save the device restarts; after restart this call returns normally.
  static void begin(const char* apName,
                    IPAddress localIp, IPAddress gateway, IPAddress subnet);

  // Erase wifi_ssid and wifi_pw from NVS. Returns true on success.
  // Call before ESP.restart() to force the next boot into AP portal mode.
  static bool clearCredentials();

private:
  static bool _tryConnect(const String& ssid, const String& pw,
                           IPAddress local, IPAddress gw, IPAddress sub);
  static void _runPortal(const char* apName);
};
