#include "ConfigPortal.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "config.h"

// Append a JSON-safe escaped string (handles quote, backslash, control chars)
static void appendEscapedJson(String& out, const String& s) {
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if      (c == '"')  { out += "\\\""; }
    else if (c == '\\') { out += "\\\\"; }
    else if (c < 0x20)  { char buf[7]; snprintf(buf, sizeof(buf), "\\u%04x", (uint8_t)c); out += buf; }
    else                { out += c; }
  }
}

// On-demand WiFi scan with 5 s cooldown (mirrors smart-exit-mat ConfigPortal).
// scanDelete() is called only AFTER processing results — never before the scan.
// Returns "[]" on failure so the caller always sends HTTP 200 (not 500).
static String scanNetworksJson() {
  static const unsigned long SCAN_COOLDOWN_MS = 5000UL;
  static const int           MAX_RAW          = 64;
  static const int           MAX_RESULTS      = 15;
  static String              s_cached;
  static unsigned long       s_cachedAt       = 0;

  unsigned long now = millis();
  if (s_cachedAt > 0 && (now - s_cachedAt) < SCAN_COOLDOWN_MS) {
    return s_cached;
  }
  s_cachedAt = now;  // update before scan so cooldown applies even on failure

  int n = WiFi.scanNetworks();  // synchronous; no pre-delete
  if (n < 0) {
    Serial.printf("[ConfigPortal] Scan failed (%d).\n", n);
    WiFi.scanDelete();
    s_cached = "[]";
    return s_cached;
  }
  if (n > MAX_RAW) n = MAX_RAW;

  // Deduplicate: keep strongest RSSI per SSID
  int idx[MAX_RAW];
  int cnt = 0;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    bool dup = false;
    for (int j = 0; j < cnt; j++) {
      if (WiFi.SSID(idx[j]) == ssid) {
        if (WiFi.RSSI(i) > WiFi.RSSI(idx[j])) idx[j] = i;
        dup = true;
        break;
      }
    }
    if (!dup) idx[cnt++] = i;
  }

  // Sort descending by RSSI
  for (int i = 1; i < cnt; i++) {
    int key = idx[i], j = i - 1;
    while (j >= 0 && WiFi.RSSI(idx[j]) < WiFi.RSSI(key)) {
      idx[j + 1] = idx[j];
      j--;
    }
    idx[j + 1] = key;
  }

  if (cnt > MAX_RESULTS) cnt = MAX_RESULTS;
  Serial.printf("[ConfigPortal] Scan complete: %d network(s) found.\n", cnt);

  String json = "[";
  for (int i = 0; i < cnt; i++) {
    if (i > 0) json += ",";
    json += "{\"s\":\"";
    appendEscapedJson(json, WiFi.SSID(idx[i]));
    json += "\",\"r\":";
    json += String(WiFi.RSSI(idx[i]));
    json += "}";
  }
  json += "]";

  WiFi.scanDelete();
  s_cached = json;
  return json;
}

static const char PORTAL_HTML[] =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'><title>FaceGuard Setup</title>"
  "<style>"
  "body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:20px}"
  "input,select{width:100%;padding:8px;margin:6px 0;box-sizing:border-box}"
  "button{width:100%;padding:10px;margin:4px 0;background:#0070f3;color:#fff;border:none;cursor:pointer}"
  ".sb{background:#555}label{display:block;margin-top:8px;font-size:.9em;color:#444}"
  "</style></head><body>"
  "<h2>FaceGuard Wi-Fi Setup</h2>"
  "<button class='sb' type='button' onclick='doScan()'>Scan Networks</button>"
  "<select id='nets' onchange='pick(this.value)'>"
  "<option value=''>-- select network --</option></select>"
  "<form method='POST' action='/save'>"
  "<label>SSID<input id='s' name='ssid' maxlength='32' required placeholder='Network name'></label>"
  "<label>Password<input type='password' name='pw' maxlength='64' placeholder='Wi-Fi password'></label>"
  "<button>Save &amp; Restart</button>"
  "</form>"
  "<p id='msg' style='font-size:.85em;color:#666'></p>"
  "<script>"
  "function pick(v){if(v)document.getElementById('s').value=v;}"
  "function doScan(){"
  "var m=document.getElementById('msg');"
  "m.textContent='Scanning...';"
  "fetch('/scan').then(function(r){"
  "if(!r.ok){m.textContent='Scan failed ('+r.status+'). Please try again.';return null;}"
  "return r.json();}).then(function(d){"
  "if(!d)return;"
  "var sel=document.getElementById('nets');"
  "sel.innerHTML='<option value=\"\">-- select network --</option>';"
  "d.forEach(function(n){"
  "var o=document.createElement('option');"
  "o.value=n.s;o.textContent=n.s+' ('+n.r+' dBm)';"
  "sel.appendChild(o);});"
  "m.textContent=d.length+' network(s) found.';}"
  ").catch(function(){m.textContent='Scan failed. Please try again.';});}"
  "</script></body></html>";

static const char SAVED_HTML[] =
  "<html><body><h2>Saved! Restarting in 2 seconds...</h2></body></html>";

bool ConfigPortal::clearCredentials() {
  Preferences prefs;
  if (!prefs.begin("agent_cfg", false)) {
    Serial.println("[ConfigPortal] ERROR: NVS open failed; credentials NOT cleared.");
    return false;
  }
  // Remove both keys unconditionally — don't short-circuit.
  // A key that doesn't exist is not an error (already cleared or first boot).
  prefs.remove("wifi_ssid");
  prefs.remove("wifi_pw");
  prefs.end();
  Serial.println("[ConfigPortal] WiFi credentials cleared from NVS.");
  return true;
}

void ConfigPortal::begin(const char* apName) {
  Preferences prefs;
  prefs.begin("agent_cfg", true);
  String ssid = prefs.getString("wifi_ssid", "");
  String pw   = prefs.getString("wifi_pw",   "");
  prefs.end();

  if (ssid.length() > 0) {
    Serial.printf("[ConfigPortal] Connecting to '%s'...\n", ssid.c_str());
    if (_tryConnect(ssid, pw)) {
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

bool ConfigPortal::_tryConnect(const String& ssid, const String& pw) {
  // Prevent ESP32 from saving credentials to its own internal flash;
  // we manage them exclusively via Preferences (NVS namespace "agent_cfg").
  WiFi.persistent(false);
  // disconnect(wifioff=true, eraseap=true): turn off WiFi AND erase any credentials
  // previously stored in the ESP32's own NVS (separate from our "agent_cfg" namespace).
  WiFi.disconnect(true, true);
  delay(400);  // allow WiFi driver to fully reset before switching mode
  WiFi.mode(WIFI_STA);
  delay(100);

  WiFi.begin(ssid.c_str(), pw.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start >= WIFI_CONNECT_TIMEOUT_MS) return false;
    delay(100);
  }

  WiFi.setAutoReconnect(true);

  Serial.printf("[ConfigPortal] IP: %s  GW: %s  DNS0: %s  DNS1: %s\n",
                WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str(),
                WiFi.dnsIP(0).toString().c_str(),
                WiFi.dnsIP(1).toString().c_str());
  return true;
}

void ConfigPortal::_runPortal(const char* apName) {
  // _tryConnect() calls esp_wifi_connect() and times out at the Arduino level,
  // leaving the IDF STA state machine in CONNECTING. esp_wifi_scan_start()
  // requires STA to be IDLE; calling disconnect() moves it there before we
  // change mode. persistent(false) prevents ESP32's own NVS from triggering
  // a background reconnect in the STA half of AP+STA mode.
  WiFi.persistent(false);
  WiFi.disconnect(false, false);  // esp_wifi_disconnect() → STA back to IDLE
  delay(100);
  // WIFI_AP_STA allows STA scan while AP is active
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(apName, "faceguard99")) {
    Serial.println("[ConfigPortal] ERROR: softAP() failed.");
    return;
  }

  Serial.printf("[ConfigPortal] AP '%s' started (pw: faceguard99). Connect and open http://%s\n",
                apName, WiFi.softAPIP().toString().c_str());

  WebServer portalServer(80);

  portalServer.on("/", HTTP_GET, [&portalServer]() {
    portalServer.send(200, "text/html", PORTAL_HTML);
  });

  portalServer.on("/scan", HTTP_GET, [&portalServer]() {
    portalServer.sendHeader("Cache-Control", "no-cache");
    portalServer.send(200, "application/json", scanNetworksJson());
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
