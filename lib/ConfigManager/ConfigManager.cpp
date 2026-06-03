#include "ConfigManager.h"
#include "config.h"
#include <Preferences.h>

static constexpr const char* NVS_NS         = "agent_cfg";
static constexpr const char* KEY_BROKER     = "mqtt_broker";
static constexpr const char* KEY_PORT       = "mqtt_port";
static constexpr const char* KEY_USER       = "mqtt_user";
static constexpr const char* KEY_PASS       = "mqtt_pw";
static constexpr const char* KEY_BUZ_FREQ   = "buzzer_freq";
static constexpr const char* KEY_BUZ_DUR    = "buzzer_dur";

static String   _broker    = "";
static uint16_t _port      = MQTT_DEFAULT_PORT;
static String   _user      = "";
static String   _pass      = "";
static uint32_t _buzFreq   = BUZZER_DEFAULT_FREQ_HZ;
static uint32_t _buzDurMs  = BUZZER_DURATION_MS;

void ConfigManager::begin() {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, true)) return;  // read-only
  _broker   = prefs.getString(KEY_BROKER, "");
  _port     = prefs.getUShort(KEY_PORT, MQTT_DEFAULT_PORT);
  _user     = prefs.getString(KEY_USER, "");
  _pass     = prefs.getString(KEY_PASS, "");
  _buzFreq  = prefs.getUInt(KEY_BUZ_FREQ, BUZZER_DEFAULT_FREQ_HZ);
  _buzDurMs = prefs.getUInt(KEY_BUZ_DUR,  BUZZER_DURATION_MS);
  prefs.end();
  // Clamp to valid ranges in case NVS is corrupted or written by a future caller
  if (_buzFreq  < 200 || _buzFreq  > 8000)   _buzFreq  = BUZZER_DEFAULT_FREQ_HZ;
  if (_buzDurMs < 10000 || _buzDurMs > 300000) _buzDurMs = BUZZER_DURATION_MS;
}

String   ConfigManager::getMqttBroker()       { return _broker; }
uint16_t ConfigManager::getMqttPort()         { return _port; }
String   ConfigManager::getMqttUsername()     { return _user; }
String   ConfigManager::getMqttPassword()     { return _pass; }
uint32_t ConfigManager::getBuzzerFreq()       { return _buzFreq; }
uint32_t ConfigManager::getBuzzerDurationMs() { return _buzDurMs; }

bool ConfigManager::save(const String& broker, uint16_t port,
                         const String& user,   const String& pass) {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) return false;
  prefs.putString(KEY_BROKER, broker);
  prefs.putUShort(KEY_PORT, port);
  prefs.putString(KEY_USER, user);

  if (pass.length() > 0) {
    // Non-empty: overwrite stored password
    prefs.putString(KEY_PASS, pass);
    _pass = pass;
  } else if (user.length() == 0) {
    // Username cleared → remove credentials entirely
    prefs.putString(KEY_PASS, "");
    _pass = "";
  }
  // empty pass + non-empty user → keep existing stored password unchanged

  prefs.end();
  _broker = broker;
  _port   = port;
  _user   = user;
  return true;
}

bool ConfigManager::saveBuzzerSettings(uint32_t freqHz, uint32_t durationMs) {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) return false;
  prefs.putUInt(KEY_BUZ_FREQ, freqHz);
  prefs.putUInt(KEY_BUZ_DUR,  durationMs);
  prefs.end();
  _buzFreq  = freqHz;
  _buzDurMs = durationMs;
  return true;
}
