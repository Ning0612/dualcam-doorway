#include "DiscordNotifier.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

unsigned long DiscordNotifier::_lastNotifyMs[DiscordNotifier::ALERT_EVENT_COUNT] = {};
unsigned long DiscordNotifier::_failCooldownStartMs = 0;

// ── Private helpers ───────────────────────────────────────────────────────────

static void _configureTls(WiFiClientSecure& client) {
#ifdef DISCORD_TLS_INSECURE
  client.setInsecure();
  Serial.println("[Discord] WARNING: TLS verification disabled (DISCORD_TLS_INSECURE).");
#else
  #ifndef DISCORD_ROOT_CA_CERT
    #error "DiscordNotifier: define DISCORD_ROOT_CA_CERT (PEM) or define DISCORD_TLS_INSECURE."
  #endif
  client.setCACert(DISCORD_ROOT_CA_CERT);
#endif
}

bool DiscordNotifier::_canSend(const String& webhookUrl, AlertEvent event) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!_isValidUrl(webhookUrl))       return false;

  if (_failCooldownStartMs != 0 &&
      millis() - _failCooldownStartMs < DISCORD_FAIL_COOLDOWN_MS) {
    Serial.println("[Discord] Skipped: failure cooldown active.");
    return false;
  }

  int idx = static_cast<int>(event);
  if (idx >= 0 && idx < ALERT_EVENT_COUNT &&
      _lastNotifyMs[idx] != 0 &&
      millis() - _lastNotifyMs[idx] < DISCORD_RATE_LIMIT_MS) {
    Serial.println("[Discord] Skipped: rate limited.");
    return false;
  }
  return true;
}

// ── Public API ────────────────────────────────────────────────────────────────

bool DiscordNotifier::notify(const String& webhookUrl, AlertEvent event,
                              const String& message) {
  if (!_canSend(webhookUrl, event)) return false;

  int idx = static_cast<int>(event);

  String masked = (webhookUrl.length() >= 8)
    ? "..." + webhookUrl.substring(webhookUrl.length() - 8) : "...";
  Serial.printf("[Discord] Sending to %s  IP: %s  DNS: %s / %s\n",
                masked.c_str(),
                WiFi.localIP().toString().c_str(),
                WiFi.dnsIP(0).toString().c_str(),
                WiFi.dnsIP(1).toString().c_str());

  JsonDocument doc;
  doc["content"] = message;
  String payload;
  serializeJson(doc, payload);

  WiFiClientSecure client;
  _configureTls(client);

  HTTPClient http;
  http.begin(client, webhookUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(DISCORD_TIMEOUT_MS);

  int code = http.POST(payload);
  http.end();

  if (code == 204 || code == 200) {
    if (idx >= 0 && idx < ALERT_EVENT_COUNT) _lastNotifyMs[idx] = millis();
    Serial.printf("[Discord] OK (HTTP %d).\n", code);
    return true;
  }

  if (code <= 0 || code == 429 || (code >= 500 && code < 600)) {
    Serial.printf("[Discord] Error (code %d). Cooldown for %lu ms.\n",
                  code, DISCORD_FAIL_COOLDOWN_MS);
    _failCooldownStartMs = millis();
  } else {
    Serial.printf("[Discord] Failed (HTTP %d).\n", code);
  }
  return false;
}

bool DiscordNotifier::notifyWithPhoto(const String& webhookUrl, AlertEvent event,
                                       const String& message,
                                       const uint8_t* jpegBuf, size_t jpegLen) {
  if (!jpegBuf || jpegLen == 0) return notify(webhookUrl, event, message);
  if (!_canSend(webhookUrl, event)) return false;

  int idx = static_cast<int>(event);

  String masked = (webhookUrl.length() >= 8)
    ? "..." + webhookUrl.substring(webhookUrl.length() - 8) : "...";
  Serial.printf("[Discord] Sending photo (%u bytes) to %s\n",
                (unsigned)jpegLen, masked.c_str());

  // ── Build multipart/form-data body ────────────────────────────────────────
  static const char* BOUNDARY = "----ESPFormBoundary7MA4YWxkTrZu0gW";

  JsonDocument doc;
  doc["content"] = message;
  String jsonStr;
  serializeJson(doc, jsonStr);

  String p1;
  p1.reserve(256);
  p1 += "--"; p1 += BOUNDARY; p1 += "\r\n";
  p1 += "Content-Disposition: form-data; name=\"payload_json\"\r\n";
  p1 += "Content-Type: application/json\r\n\r\n";
  p1 += jsonStr; p1 += "\r\n";

  String p2;
  p2.reserve(128);
  p2 += "--"; p2 += BOUNDARY; p2 += "\r\n";
  p2 += "Content-Disposition: form-data; name=\"files[0]\"; filename=\"alert.jpg\"\r\n";
  p2 += "Content-Type: image/jpeg\r\n\r\n";

  String p3;
  p3 += "\r\n--"; p3 += BOUNDARY; p3 += "--\r\n";

  size_t totalLen = p1.length() + p2.length() + jpegLen + p3.length();

  uint8_t* body = (uint8_t*)heap_caps_malloc(totalLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!body) body = (uint8_t*)malloc(totalLen);
  if (!body) {
    Serial.println("[Discord] Photo: alloc failed, falling back to text-only.");
    return notify(webhookUrl, event, message);
  }

  size_t off = 0;
  memcpy(body + off, p1.c_str(), p1.length()); off += p1.length();
  memcpy(body + off, p2.c_str(), p2.length()); off += p2.length();
  memcpy(body + off, jpegBuf,    jpegLen);      off += jpegLen;
  memcpy(body + off, p3.c_str(), p3.length());

  WiFiClientSecure client;
  _configureTls(client);

  HTTPClient http;
  http.begin(client, webhookUrl);
  String ct = "multipart/form-data; boundary=";
  ct += BOUNDARY;
  http.addHeader("Content-Type", ct);
  http.setTimeout(DISCORD_TIMEOUT_MS);

  int code = http.POST(body, totalLen);
  free(body);
  http.end();

  if (code == 204 || code == 200) {
    if (idx >= 0 && idx < ALERT_EVENT_COUNT) _lastNotifyMs[idx] = millis();
    Serial.printf("[Discord] Photo OK (HTTP %d).\n", code);
    return true;
  }

  if (code <= 0 || code == 429 || (code >= 500 && code < 600)) {
    Serial.printf("[Discord] Photo error (code %d). Cooldown for %lu ms.\n",
                  code, DISCORD_FAIL_COOLDOWN_MS);
    _failCooldownStartMs = millis();
  } else {
    Serial.printf("[Discord] Photo failed (HTTP %d).\n", code);
  }
  return false;
}

bool DiscordNotifier::notifyBoot(const String& webhookUrl, const String& message) {
  // Intentionally no cooldown or rate-limit check: boot notification must not
  // pollute _failCooldownStartMs or _lastNotifyMs and thereby suppress security alerts.
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!_isValidUrl(webhookUrl))       return false;

  JsonDocument doc;
  doc["content"] = message;
  String payload;
  serializeJson(doc, payload);

  WiFiClientSecure client;
  _configureTls(client);

  HTTPClient http;
  http.begin(client, webhookUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(DISCORD_TIMEOUT_MS);

  int code = http.POST(payload);
  http.end();

  if (code == 204 || code == 200) {
    Serial.printf("[Discord] Boot notify OK (HTTP %d).\n", code);
    return true;
  }
  Serial.printf("[Discord] Boot notify failed (HTTP %d) — security alerts unaffected.\n", code);
  return false;
}

bool DiscordNotifier::_isValidUrl(const String& url) {
  return url.startsWith("https://discord.com/api/webhooks/") ||
         url.startsWith("https://discordapp.com/api/webhooks/");
}
