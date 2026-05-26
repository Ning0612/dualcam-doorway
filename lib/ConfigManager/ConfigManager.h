#pragma once
#include <Arduino.h>

// Manages MQTT connection settings stored in NVS (namespace "agent_cfg").
// Hall-effect threshold is managed by SettingsStore (already present there).
class ConfigManager {
public:
  static void begin();

  static String   getMqttBroker();   // "" if not configured
  static uint16_t getMqttPort();     // default MQTT_DEFAULT_PORT

  // Persist settings to NVS. Pass "" for broker to clear.
  static bool save(const String& broker, uint16_t port);
};
