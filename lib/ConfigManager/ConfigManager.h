#pragma once
#include <Arduino.h>

// Manages MQTT connection settings stored in NVS (namespace "agent_cfg").
// Hall-effect threshold is managed by SettingsStore (already present there).
class ConfigManager {
public:
  static void begin();

  static String   getMqttBroker();    // "" if not configured
  static uint16_t getMqttPort();      // default MQTT_DEFAULT_PORT
  static String   getMqttUsername();  // "" if no auth
  static String   getMqttPassword();  // "" if no auth
  static uint32_t getBuzzerFreq();         // Hz; default BUZZER_DEFAULT_FREQ_HZ
  static uint32_t getBuzzerDurationMs();   // ms; default BUZZER_DURATION_MS

  // Persist settings to NVS. Pass "" for broker to clear.
  // Pass "" for user to clear username (no auth).
  // Pass "" for pass to KEEP existing stored password unchanged.
  static bool save(const String& broker, uint16_t port,
                   const String& user, const String& pass);
  static bool saveBuzzerSettings(uint32_t freqHz, uint32_t durationMs);
};
