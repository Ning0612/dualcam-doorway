#include "DashboardServer.h"
#include "DoorStateMachine.h"
#include "SessionAuth.h"
#include "SettingsStore.h"
#include "CameraAgent.h"
#include "FaceRecognizer.h"
#include "config.h"
#include "states.h"
#include <ArduinoJson.h>

// ── HTML templates ────────────────────────────────────────────────────────────

static const char DASHBOARD_HTML[] =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'><title>DualCam - %AGENT%</title>"
  "<style>body{font-family:sans-serif;max-width:600px;margin:20px auto;padding:20px}"
  ".card{background:#f5f5f5;border-radius:8px;padding:16px;margin:12px 0}"
  ".st{font-size:1.4em;font-weight:bold}.lbl{color:#666;font-size:.85em}"
  "a{color:#0070f3}nav{margin-bottom:16px}"
  ".hidden{display:none}</style></head><body>"
  "<h2>DualCam &mdash; %AGENT%</h2>"
  "<nav><a href='/settings'>Settings</a> | <a href='/logout'>Logout</a></nav>"
  "<div class='card'><div class='lbl'>State</div><div class='st' id='st'>&mdash;</div></div>"
  "<div class='card'><div class='lbl'>Door</div><div id='door'>&mdash;</div></div>"
  "<div id='hall_card' class='card hidden'>"
  "<div class='lbl'>Hall Sensor</div><div id='hall'>&mdash;</div></div>"
  "<div class='card'><div class='lbl'>Peer</div><div id='peer'>&mdash;</div></div>"
  "<div class='card'><div class='lbl'>Uptime</div><div id='up'>&mdash;</div></div>"
  "<div class='card'><div class='lbl'>Face Recognition</div>"
  "<div id='frc'>&mdash;</div>"
  "<div style='margin-top:8px'>"
  "<button onclick='fr_enroll()' id='fr_eb' style='padding:5px 10px'>Enroll Face</button>"
  "<button onclick='fr_clr()' style='padding:5px 10px;margin-left:6px'>Clear All</button>"
  "</div><div id='fr_msg' class='lbl'></div></div>"
  "<div id='csrf_v' style='display:none'>%CSRF%</div>"
  "<script>"
  "function fmt(ms){var s=Math.floor(ms/1000);"
  "return Math.floor(s/3600)+'h '+Math.floor(s%3600/60)+'m '+s%60+'s';}"
  "function poll(){fetch('/api/status').then(r=>r.json()).then(d=>{"
  "document.getElementById('st').textContent=d.state||'?';"
  "document.getElementById('door').textContent=d.door||'?';"
  "document.getElementById('peer').textContent="
  "d.peer_online?(d.peer_state||'?'):'offline';"
  "document.getElementById('up').textContent=fmt(d.uptime||0);"
  "if(d.hall_raw!==undefined){"
  "document.getElementById('hall_card').classList.remove('hidden');"
  "document.getElementById('hall').textContent="
  "'raw: '+d.hall_raw+' / threshold: '+d.hall_threshold;}"
  "if(d.face_count!==undefined){"
  "document.getElementById('frc').textContent='Enrolled: '+d.face_count+'/'+d.face_max;}"
  "}).catch(()=>{}); }"
  "function fr_enroll(){"
  "var c=document.getElementById('csrf_v').textContent;"
  "document.getElementById('fr_eb').disabled=true;"
  "document.getElementById('fr_msg').textContent='Stand in front of camera...';"
  "fetch('/api/face/enroll',{method:'POST',"
  "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
  "body:'csrf='+encodeURIComponent(c)})"
  ".then(r=>r.json()).then(function(d){"
  "document.getElementById('fr_msg').textContent="
  "d.error?('Error: '+d.error):'Scheduled - face will be enrolled on next detection';"
  "document.getElementById('fr_eb').disabled=false;"
  "}).catch(function(){document.getElementById('fr_eb').disabled=false;});}"
  "function fr_clr(){"
  "if(!confirm('Clear all enrolled faces?'))return;"
  "var c=document.getElementById('csrf_v').textContent;"
  "fetch('/api/face/clear',{method:'POST',"
  "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
  "body:'csrf='+encodeURIComponent(c)})"
  ".then(r=>r.json()).then(function(){"
  "document.getElementById('fr_msg').textContent='All faces cleared.';}).catch(function(){});}"
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
  "%HALL_FIELD%"
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
static bool*             _doorOpen      = nullptr;
static uint16_t*         _hallRaw       = nullptr;
static uint16_t*         _hallThreshold = nullptr;

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
                             bool* doorOpen,
                             uint16_t* hallRaw,
                             uint16_t* hallThreshold) {
  _sm            = &sm;
  _agentLabel    = agentLabel;
  _cachedPeer    = cachedPeer;
  _doorOpen      = doorOpen;
  _hallRaw       = hallRaw;
  _hallThreshold = hallThreshold;

  // Root → dashboard
  server.on("/", HTTP_GET, [&server]() {
    server.sendHeader("Location", "/dashboard");
    server.send(302, "text/plain", "");
  });

  // Suppress browser auto-request log noise
  server.on("/favicon.ico", HTTP_GET, [&server]() {
    server.send(204, "text/plain", "");
  });

  // ── Dashboard page ──────────────────────────────────────────────────────────
  server.on("/dashboard", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    String page = DASHBOARD_HTML;
    page.replace("%AGENT%", _agentLabel ? _agentLabel : "");
    page.replace("%CSRF%", SessionAuth::getCsrfToken());
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

    if (_hallRaw) {
      doc["hall_raw"]       = *_hallRaw;
      doc["hall_threshold"] = SettingsStore::getHallThreshold();
    }

    doc["face_count"] = FaceRecognizer::count();
    doc["face_max"]   = FaceRecognizer::MAX_FACES;

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
    if (_hallRaw) {
      String hf = "<fieldset><legend>Hall Sensor Threshold</legend>"
                  "<label>Threshold (0-4095)<br>"
                  "<input type='number' name='hall_threshold' min='0' max='4095' value='";
      hf += SettingsStore::getHallThreshold();
      hf += "'></label><p style='font-size:.8em;color:#666'>"
            "Current raw: " + String(*_hallRaw) + " &mdash; "
            "set between closed and open readings.</p></fieldset>";
      page.replace("%HALL_FIELD%", hf);
    } else {
      page.replace("%HALL_FIELD%", "");
    }
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

    // Hall threshold (indoor only) — validate before saving
    if (_hallRaw && server.hasArg("hall_threshold")) {
      String hallArg = server.arg("hall_threshold");
      bool numeric = hallArg.length() > 0;
      for (size_t i = 0; i < hallArg.length() && numeric; i++) {
        if (!isdigit((unsigned char)hallArg[i])) numeric = false;
      }
      if (!numeric) {
        msg += "<p class='err'>Hall threshold must be a number.</p>";
      } else {
        int val = hallArg.toInt();
        if (!SettingsStore::setHallThreshold((uint16_t)val)) {
          msg += "<p class='err'>Hall threshold must be between " +
                 String(HALL_HYSTERESIS + 1) + " and " +
                 String(4095 - HALL_HYSTERESIS) + ".</p>";
        } else {
          if (_hallThreshold) *_hallThreshold = (uint16_t)val;  // update runtime value immediately
          msg += "<p class='ok'>Hall threshold saved.</p>";
        }
      }
    }

    String page = SETTINGS_HTML;
    page.replace("%MSG%",        msg);
    page.replace("%CSRF%",       SessionAuth::getCsrfToken());
    page.replace("%DISCORD_URL%", htmlAttrEscape(SettingsStore::getDiscordUrl()));
    if (_hallRaw) {
      String hf = "<fieldset><legend>Hall Sensor Threshold</legend>"
                  "<label>Threshold (0-4095)<br>"
                  "<input type='number' name='hall_threshold' min='0' max='4095' value='";
      hf += SettingsStore::getHallThreshold();
      hf += "'></label><p style='font-size:.8em;color:#666'>"
            "Current raw: " + String(*_hallRaw) + " &mdash; "
            "set between closed and open readings.</p></fieldset>";
      page.replace("%HALL_FIELD%", hf);
    } else {
      page.replace("%HALL_FIELD%", "");
    }
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

  // ── Face management API ─────────────────────────────────────────────────────
  server.on("/api/face/enroll", HTTP_POST, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    if (!SessionAuth::verifyCsrf(server.arg("csrf"))) {
      server.send(403, "application/json", "{\"error\":\"CSRF\"}");
      return;
    }
    if (!CameraAgent::isInitialized()) {
      server.send(503, "application/json", "{\"error\":\"camera not ready\"}");
      return;
    }
    if (FaceRecognizer::count() >= FaceRecognizer::MAX_FACES) {
      server.send(409, "application/json", "{\"error\":\"face bank full\"}");
      return;
    }
    CameraAgent::scheduleEnroll();
    JsonDocument doc;
    doc["scheduled"] = true;
    doc["count"]     = FaceRecognizer::count();
    doc["max"]       = FaceRecognizer::MAX_FACES;
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  server.on("/api/face/clear", HTTP_POST, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    if (!SessionAuth::verifyCsrf(server.arg("csrf"))) {
      server.send(403, "application/json", "{\"error\":\"CSRF\"}");
      return;
    }
    CameraAgent::cancelEnroll();  // abort any pending enroll before clearing
    bool ok = FaceRecognizer::clearAll();
    JsonDocument doc;
    doc["cleared"] = ok;
    if (!ok) doc["error"] = "NVS write failed — reboot may restore old faces";
    String json;
    serializeJson(doc, json);
    server.send(ok ? 200 : 500, "application/json", json);
  });

  // ── 404 fallback ────────────────────────────────────────────────────────────
  server.onNotFound([&server]() {
    server.sendHeader("Location", "/dashboard");
    server.send(302, "text/plain", "");
  });
}
