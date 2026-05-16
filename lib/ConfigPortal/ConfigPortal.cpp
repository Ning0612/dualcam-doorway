#include "ConfigPortal.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "config.h"

static const char PORTAL_HTML[] =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'><title>DualCam Setup</title>"
  "<style>body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:20px}"
  "input{width:100%;padding:8px;margin:8px 0;box-sizing:border-box}"
  "button{width:100%;padding:10px;background:#0070f3;color:#fff;border:none;cursor:pointer}"
  "</style></head><body>"
  "<h2>DualCam Wi-Fi Setup</h2>"
  "<form method='POST' action='/save'>"
  "<label>Wi-Fi SSID<br><input name='ssid' maxlength='32' required></label>"
  "<label>Password<br><input type='password' name='pw' maxlength='64'></label>"
  "<button>Save &amp; Restart</button>"
  "</form></body></html>";

static const char SAVED_HTML[] =
  "<html><body><h2>Saved! Restarting in 2 seconds...</h2></body></html>";

void ConfigPortal::begin(const char* apName,
                          IPAddress localIp, IPAddress gateway, IPAddress subnet) {
  Preferences prefs;
  prefs.begin("agent_cfg", true);
  String ssid = prefs.getString("wifi_ssid", "");
  String pw   = prefs.getString("wifi_pw",   "");
  prefs.end();

  if (ssid.length() > 0) {
    Serial.printf("[ConfigPortal] Connecting to '%s'...\n", ssid.c_str());
    if (_tryConnect(ssid, pw, localIp, gateway, subnet)) {
      Serial.println("[ConfigPortal] Connected.");
      return;
    }
    Serial.println("[ConfigPortal] Connect failed, entering portal.");
  } else {
    Serial.println("[ConfigPortal] No credentials stored, entering portal.");
  }

  _runPortal(apName);

  // Portal timed out without a save → restart and retry
  Serial.println("[ConfigPortal] Portal timeout. Restarting.");
  ESP.restart();
}

bool ConfigPortal::_tryConnect(const String& ssid, const String& pw,
                                 IPAddress local, IPAddress gw, IPAddress sub) {
  WiFi.mode(WIFI_STA);
  if (!WiFi.config(local, gw, sub)) {
    Serial.println("[ConfigPortal] ERROR: WiFi.config() failed; cannot assign static IP.");
    return false;
  }
  WiFi.begin(ssid.c_str(), pw.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start >= WIFI_CONNECT_TIMEOUT_MS) return false;
    delay(100);
  }
  return true;
}

void ConfigPortal::_runPortal(const char* apName) {
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(apName, "dualcam99")) {
    Serial.println("[ConfigPortal] ERROR: softAP() failed.");
    return;
  }

  Serial.printf("[ConfigPortal] AP '%s' started. Connect and open http://%s\n",
                apName, WiFi.softAPIP().toString().c_str());

  WebServer portalServer(80);

  portalServer.on("/", HTTP_GET, [&portalServer]() {
    portalServer.send(200, "text/html", PORTAL_HTML);
  });

  bool saved = false;

  portalServer.on("/save", HTTP_POST, [&]() {
    String newSsid = portalServer.arg("ssid");
    String newPw   = portalServer.arg("pw");

    if (newSsid.length() == 0 || newSsid.length() > 32) {
      portalServer.send(400, "text/plain", "SSID must be 1-32 characters.");
      return;
    }
    if (newPw.length() > 64) {
      portalServer.send(400, "text/plain", "Password too long (max 64).");
      return;
    }

    Preferences prefs;
    if (!prefs.begin("agent_cfg", false)) {
      Serial.println("[ConfigPortal] ERROR: NVS begin() failed.");
      portalServer.send(500, "text/plain", "Storage error. Please retry.");
      return;
    }
    prefs.putString("wifi_ssid", newSsid);
    prefs.putString("wifi_pw",   newPw);
    prefs.end();

    Serial.printf("[ConfigPortal] Saved SSID: %s\n", newSsid.c_str());
    portalServer.send(200, "text/html", SAVED_HTML);
    saved = true;
  });

  portalServer.begin();

  unsigned long start = millis();
  while (!saved && (millis() - start < PORTAL_TIMEOUT_MS)) {
    portalServer.handleClient();
    delay(5);
  }

  if (saved) {
    delay(2000);
    ESP.restart();
  }
  // If not saved, caller handles the timeout (restart)
}
