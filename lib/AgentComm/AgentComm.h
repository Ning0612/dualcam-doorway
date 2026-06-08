#pragma once
#include <Arduino.h>
#include <PubSubClient.h>
#include "states.h"

// MQTT communication layer for FaceGuard.
//
// Publishes security events and subscribes to Agent 2 presence and alarm decisions.
// Reconnects automatically every MQTT_RECONNECT_MS when disconnected.
// Re-subscribes immediately after each successful reconnect.
//
// If broker IP is empty (""), begin() is a no-op and isConnected() returns false.
// In that case the system defaults to ALERT_RED (Agent 2 considered offline).
class AgentComm {
public:
  // Call once after WiFi is connected.
  // username/password: pass empty strings for unauthenticated brokers.
  // clientId: unique MQTT identifier; rarely changed from the default.
  // NOTE: parameter order changed from original (clientId moved to last).
  static void begin(const char* broker, uint16_t port,
                    const char* username = "", const char* password = "",
                    const char* clientId = "faceguard");

  // Call every loop() iteration: drains the cross-task event queue and fires callbacks.
  // PubSubClient::loop() and reconnect logic run in a background FreeRTOS task (Core 0).
  static void tick();

  // ── Publish ───────────────────────────────────────────────────────────────
  static bool publishDoor(DoorState state, const char* relatedUser = nullptr);
  static bool publishFace(VoteResult vote, const char* userName = nullptr, float similarity = 0.0f);
  static bool publishAlert(AlertLevel level, const char* alertType);
  static bool publishStatus(AlertLevel level, unsigned long uptime);

  // Publish a raw JPEG frame to MQTT_TOPIC_CAMERA.
  // Uses PubSubClient streaming API — not limited by MQTT_MAX_PACKET_SIZE.
  // Best-effort: returns false (drops frame) if MQTT is busy or disconnected.
  static bool publishCamera(const uint8_t* buf, size_t len);

  // ── Subscribe callbacks (set before begin()) ──────────────────────────────
  // Called when a valid presence message arrives from Agent 2.
  static void setOnPresence(void (*cb)(bool occupied, int score));

  // Called when a valid alarm_decision message arrives from Agent 2.
  static void setOnAlarmDecision(void (*cb)(AlarmDecision decision));

  // Called when Agent 2 sends a proactive alarm_command (TRIGGER or CANCEL).
  // Unlike alarm_decision, this is NOT guarded by _waitingForDecision — it
  // always takes effect regardless of current state.
  static void setOnAlarmCommand(void (*cb)(AlarmDecision decision));

  // Called when Agent 2 presence online state changes (online = true/false).
  // Fires true on first presence heartbeat after offline; fires false on presence
  // timeout (AGENT2_OFFLINE_TIMEOUT_MS) or MQTT broker disconnect.
  // NOTE: broker connected ≠ Agent 2 online — this tracks heartbeat freshness, not broker state.
  static void setOnConnectionChange(void (*cb)(bool connected));

  static bool isConnected();

private:
  static void _onMessage(const char* topic, byte* payload, unsigned int len);
  static void _mqttTaskFn(void* pvParameters);
  static bool _publish(const char* topic, const String& payload);
};
