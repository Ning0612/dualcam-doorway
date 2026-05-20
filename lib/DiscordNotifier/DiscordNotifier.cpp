#include "DiscordNotifier.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

unsigned long DiscordNotifier::_lastNotifyMs[9]  = {};
unsigned long DiscordNotifier::_failCooldownUntil = 0;

bool DiscordNotifier::notify(const String& webhookUrl, SystemState forState,
                              const String& message) {
  // ── Fail-fast checks (no network I/O) ──────────────────────────────────────
  if (WiFi.status() != WL_CONNECTED)  return false;
  if (!_isValidUrl(webhookUrl))        return false;

  if (millis() < _failCooldownUntil) {
    Serial.println("[Discord] Skipped: failure cooldown active.");
    return false;
  }

  int idx = static_cast<int>(forState);
  if (idx >= 0 && idx < 9) {
    if (_lastNotifyMs[idx] != 0 && millis() - _lastNotifyMs[idx] < DISCORD_RATE_LIMIT_MS) {
      Serial.println("[Discord] Skipped: rate limited.");
      return false;
    }
  }

  // ── Log with masked URL (last 8 chars only) ─────────────────────────────────
  String masked = (webhookUrl.length() >= 8)
    ? "..." + webhookUrl.substring(webhookUrl.length() - 8)
    : "...";
  Serial.printf("[Discord] Sending to %s  IP: %s  DNS: %s / %s\n",
                masked.c_str(),
                WiFi.localIP().toString().c_str(),
                WiFi.dnsIP(0).toString().c_str(),
                WiFi.dnsIP(1).toString().c_str());

  // ── Build JSON payload ──────────────────────────────────────────────────────
  JsonDocument doc;
  doc["content"] = message;
  String payload;
  serializeJson(doc, payload);

  // ── HTTPS request ───────────────────────────────────────────────────────────
  WiFiClientSecure client;
#ifdef DISCORD_TLS_INSECURE
  client.setInsecure();
  Serial.println("[Discord] WARNING: TLS verification disabled (DISCORD_TLS_INSECURE).");
#else
  #ifndef DISCORD_ROOT_CA_CERT
    #error "DiscordNotifier: define DISCORD_ROOT_CA_CERT (PEM) or define DISCORD_TLS_INSECURE."
  #endif
  client.setCACert(DISCORD_ROOT_CA_CERT);
#endif

  HTTPClient http;
  http.begin(client, webhookUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(DISCORD_TIMEOUT_MS);

  int code = http.POST(payload);
  http.end();

  // Discord returns 204 No Content on success (200 in some rare cases)
  if (code == 204 || code == 200) {
    if (idx >= 0 && idx < 9) _lastNotifyMs[idx] = millis();
    Serial.printf("[Discord] OK (HTTP %d).\n", code);
    return true;
  }

  if (code <= 0 || code == 429 || (code >= 500 && code < 600)) {
    Serial.printf("[Discord] Error (code %d). Cooldown for %lu ms.\n",
                  code, DISCORD_FAIL_COOLDOWN_MS);
    _failCooldownUntil = millis() + DISCORD_FAIL_COOLDOWN_MS;
  } else {
    Serial.printf("[Discord] Failed (HTTP %d).\n", code);
  }
  return false;
}

bool DiscordNotifier::notifyBoot(const String& webhookUrl, const String& message) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!_isValidUrl(webhookUrl))       return false;

  JsonDocument doc;
  doc["content"] = message;
  String payload;
  serializeJson(doc, payload);

  WiFiClientSecure client;
#ifdef DISCORD_TLS_INSECURE
  client.setInsecure();
#else
  client.setCACert(DISCORD_ROOT_CA_CERT);
#endif

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
