#include "ConfigManager.h"
#include "config.h"
#include <Preferences.h>

static constexpr const char* NVS_NS        = "agent_cfg";
static constexpr const char* KEY_BROKER    = "mqtt_broker";
static constexpr const char* KEY_PORT      = "mqtt_port";

static String   _broker = "";
static uint16_t _port   = MQTT_DEFAULT_PORT;

void ConfigManager::begin() {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, true)) return;  // read-only
  _broker = prefs.getString(KEY_BROKER, "");
  _port   = prefs.getUShort(KEY_PORT, MQTT_DEFAULT_PORT);
  prefs.end();
}

String   ConfigManager::getMqttBroker() { return _broker; }
uint16_t ConfigManager::getMqttPort()   { return _port; }

bool ConfigManager::save(const String& broker, uint16_t port) {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) return false;
  prefs.putString(KEY_BROKER, broker);
  prefs.putUShort(KEY_PORT, port);
  prefs.end();
  _broker = broker;
  _port   = port;
  return true;
}
