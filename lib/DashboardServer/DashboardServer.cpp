#include "DashboardServer.h"
#include "DoorStateMachine.h"
#include "SessionAuth.h"
#include "SettingsStore.h"
#include "states.h"
#include <ArduinoJson.h>

// ── HTML templates ────────────────────────────────────────────────────────────

static const char DASHBOARD_HTML[] =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'><title>DualCam - %AGENT%</title>"
  "<style>body{font-family:sans-serif;max-width:600px;margin:20px auto;padding:20px}"
  ".card{background:#f5f5f5;border-radius:8px;padding:16px;margin:12px 0}"
  ".st{font-size:1.4em;font-weight:bold}.lbl{color:#666;font-size:.85em}"
  "a{color:#0070f3}nav{margin-bottom:16px}</style></head><body>"
  "<h2>DualCam &mdash; %AGENT%</h2>"
  "<nav><a href='/settings'>Settings</a> | <a href='/logout'>Logout</a></nav>"
  "<div class='card'><div class='lbl'>State</div><div class='st' id='st'>&mdash;</div></div>"
  "<div class='card'><div class='lbl'>Door</div><div id='door'>&mdash;</div></div>"
  "<div class='card'><div class='lbl'>Peer</div><div id='peer'>&mdash;</div></div>"
  "<div class='card'><div class='lbl'>Uptime</div><div id='up'>&mdash;</div></div>"
  "<script>"
  "function fmt(ms){var s=Math.floor(ms/1000);"
  "return Math.floor(s/3600)+'h '+Math.floor(s%3600/60)+'m '+s%60+'s';}"
  "function poll(){fetch('/api/status').then(r=>r.json()).then(d=>{"
  "document.getElementById('st').textContent=d.state||'?';"
  "document.getElementById('door').textContent=d.door||'?';"
  "document.getElementById('peer').textContent="
  "d.peer_online?(d.peer_state||'?'):'offline';"
  "document.getElementById('up').textContent=fmt(d.uptime||0);"
  "}).catch(()=>{}); }"
  "poll();setInterval(poll,3000);"
  "</script></body></html>";

static const char SETTINGS_HTML[] =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'><title>Settings - DualCam</title>"
  "<style>body{font-family:sans-serif;max-width:500px;margin:20px auto;padding:20px}"
  "fieldset{border:1px solid #ddd;border-radius:6px;padding:12px;margin:12px 0}"
  "legend{font-weight:bold}"
  "input{width:100%;padding:8px;margin:6px 0;box-sizing:border-box}"
  "button{padding:10px 20px;background:#0070f3;color:#fff;border:none;cursor:pointer}"
  "a{color:#0070f3}.ok{color:green;font-size:.9em}.err{color:red;font-size:.9em}"
  "</style></head><body>"
  "<h2>Settings</h2><a href='/dashboard'>&larr; Dashboard</a>"
  "%MSG%"
  "<form method='POST' action='/settings/save'>"
  "<input type='hidden' name='csrf' value='%CSRF%'>"
  "<fieldset><legend>Change Password</legend>"
  "<label>New Password (8-64 chars)<br>"
  "<input type='password' name='newpw' minlength='8' maxlength='64'></label>"
  "<label>Confirm<br>"
  "<input type='password' name='confirmpw' maxlength='64'></label>"
  "</fieldset>"
  "<fieldset><legend>Discord Webhook</legend>"
  "<label>URL<br>"
  "<input name='discord_url' maxlength='256' "
  "placeholder='https://discord.com/api/webhooks/...' value='%DISCORD_URL%'></label>"
  "</fieldset>"
  "<button>Save</button>"
  "</form></body></html>";

static const char PWCHANGE_HTML[] =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'><title>Set Password - DualCam</title>"
  "<style>body{font-family:sans-serif;max-width:400px;margin:60px auto;padding:20px}"
  "input{width:100%;padding:8px;margin:6px 0;box-sizing:border-box}"
  "button{width:100%;padding:10px;background:#0070f3;color:#fff;border:none;cursor:pointer}"
  ".err{color:red;font-size:.9em}</style></head><body>"
  "<h2>Set Admin Password</h2>"
  "<p>Please set a new password before continuing.</p>"
  "%ERR%"
  "<form method='POST' action='/password/save'>"
  "<input type='hidden' name='csrf' value='%CSRF%'>"
  "<label>New Password (8-64 chars)<br>"
  "<input type='password' name='newpw' minlength='8' maxlength='64' required></label>"
  "<label>Confirm<br>"
  "<input type='password' name='confirmpw' maxlength='64' required></label>"
  "<button>Set Password</button>"
  "</form></body></html>";

// ── Module-level state ────────────────────────────────────────────────────────

static DoorStateMachine* _sm          = nullptr;
static const char*       _agentLabel  = nullptr;
static PeerStatus*       _cachedPeer  = nullptr;
static bool*             _doorOpen    = nullptr;

// ── Helpers ───────────────────────────────────────────────────────────────────

static String htmlAttrEscape(const String& s) {
  String out;
  out.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if      (c == '&')  out += "&amp;";
    else if (c == '"')  out += "&quot;";
    else if (c == '\'') out += "&#39;";
    else if (c == '<')  out += "&lt;";
    else if (c == '>')  out += "&gt;";
    else                out += c;
  }
  return out;
}

static bool requireAuth(WebServer& server) {
  if (!SessionAuth::isAuthorized(server)) {
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "");
    return false;
  }
  return true;
}

static bool requireAuthAndChangedPassword(WebServer& server) {
  if (!SessionAuth::isAuthorized(server)) {
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "");
    return false;
  }
  if (SettingsStore::hasDefaultPassword()) {
    server.sendHeader("Location", "/password/change");
    server.send(302, "text/plain", "");
    return false;
  }
  return true;
}

// ── Public API ────────────────────────────────────────────────────────────────

void DashboardServer::begin(WebServer& server,
                             DoorStateMachine& sm,
                             const char* agentLabel,
                             PeerStatus* cachedPeer,
                             bool* doorOpen) {
  _sm         = &sm;
  _agentLabel = agentLabel;
  _cachedPeer = cachedPeer;
  _doorOpen   = doorOpen;

  // Root → dashboard
  server.on("/", HTTP_GET, [&server]() {
    server.sendHeader("Location", "/dashboard");
    server.send(302, "text/plain", "");
  });

  // ── Dashboard page ──────────────────────────────────────────────────────────
  server.on("/dashboard", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    String page = DASHBOARD_HTML;
    page.replace("%AGENT%", _agentLabel ? _agentLabel : "");
    server.send(200, "text/html", page);
  });

  // ── Status API ──────────────────────────────────────────────────────────────
  server.on("/api/status", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;

    JsonDocument doc;
    doc["state"]  = stateToString(_sm->getState());
    doc["door"]   = _doorOpen ? (*_doorOpen ? "open" : "closed") : "n/a";
    doc["uptime"] = millis();

    if (_cachedPeer) {
      doc["peer_online"] = _cachedPeer->online;
      doc["peer_state"]  = stateToString(_cachedPeer->state);
    } else {
      doc["peer_online"] = false;
      doc["peer_state"]  = "unknown";
    }

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  // ── Settings page ───────────────────────────────────────────────────────────
  server.on("/settings", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    String page = SETTINGS_HTML;
    page.replace("%MSG%", "");
    page.replace("%CSRF%", SessionAuth::getCsrfToken());
    page.replace("%DISCORD_URL%", htmlAttrEscape(SettingsStore::getDiscordUrl()));
    server.send(200, "text/html", page);
  });

  // ── Settings save ───────────────────────────────────────────────────────────
  server.on("/settings/save", HTTP_POST, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    if (!SessionAuth::verifyCsrf(server.arg("csrf"))) {
      server.send(403, "text/plain", "CSRF validation failed.");
      return;
    }

    String msg;
    String newPw      = server.arg("newpw");
    String confirmPw  = server.arg("confirmpw");
    String discordUrl = server.arg("discord_url");

    if (newPw.length() > 0) {
      if (newPw != confirmPw) {
        msg += "<p class='err'>Passwords do not match.</p>";
      } else if (!SettingsStore::setDashboardPassword(newPw)) {
        msg += "<p class='err'>Password must be 8-64 characters.</p>";
      } else {
        msg += "<p class='ok'>Password updated.</p>";
      }
    }

    // Empty discord_url field = clear; non-empty = validate and save
    if (server.hasArg("discord_url")) {
      if (discordUrl.length() == 0) {
        SettingsStore::setDiscordUrl("");
      } else if (!SettingsStore::setDiscordUrl(discordUrl)) {
        msg += "<p class='err'>Invalid Discord URL (must start with "
               "https://discord.com/api/webhooks/).</p>";
      } else {
        if (msg.indexOf("err") < 0) msg += "<p class='ok'>Discord URL saved.</p>";
      }
    }

    String page = SETTINGS_HTML;
    page.replace("%MSG%",        msg);
    page.replace("%CSRF%",       SessionAuth::getCsrfToken());
    page.replace("%DISCORD_URL%", htmlAttrEscape(SettingsStore::getDiscordUrl()));
    server.send(200, "text/html", page);
  });

  // ── Forced password change ──────────────────────────────────────────────────
  server.on("/password/change", HTTP_GET, [&server]() {
    if (!requireAuth(server)) return;
    String page = PWCHANGE_HTML;
    page.replace("%ERR%",  "");
    page.replace("%CSRF%", SessionAuth::getCsrfToken());
    server.send(200, "text/html", page);
  });

  server.on("/password/save", HTTP_POST, [&server]() {
    if (!requireAuth(server)) return;
    if (!SessionAuth::verifyCsrf(server.arg("csrf"))) {
      server.send(403, "text/plain", "CSRF validation failed.");
      return;
    }

    String newPw     = server.arg("newpw");
    String confirmPw = server.arg("confirmpw");

    if (newPw != confirmPw) {
      String page = PWCHANGE_HTML;
      page.replace("%ERR%",  "<p class='err'>Passwords do not match.</p>");
      page.replace("%CSRF%", SessionAuth::getCsrfToken());
      server.send(200, "text/html", page);
      return;
    }

    if (!SettingsStore::setDashboardPassword(newPw)) {
      String page = PWCHANGE_HTML;
      page.replace("%ERR%",  "<p class='err'>Password must be 8-64 characters.</p>");
      page.replace("%CSRF%", SessionAuth::getCsrfToken());
      server.send(200, "text/html", page);
      return;
    }

    server.sendHeader("Location", "/dashboard");
    server.send(302, "text/plain", "");
  });

  // ── 404 fallback ────────────────────────────────────────────────────────────
  server.onNotFound([&server]() {
    server.sendHeader("Location", "/dashboard");
    server.send(302, "text/plain", "");
  });
}
