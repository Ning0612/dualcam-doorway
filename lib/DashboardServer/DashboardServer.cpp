#include "DashboardServer.h"
#include "SessionAuth.h"
#include "SettingsStore.h"
#include "ConfigManager.h"
#include "DoorSensor.h"
#include "CameraAgent.h"
#include "FaceRecognizer.h"
#include "config.h"
#include "states.h"
#include <ArduinoJson.h>

// ── HTML templates ────────────────────────────────────────────────────────────

static const char DASHBOARD_HTML[] =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'><title>Agent 1 - %AGENT%</title>"
  "<style>"
  "body{font-family:sans-serif;max-width:820px;margin:20px auto;padding:20px}"
  ".card{background:#f5f5f5;border-radius:8px;padding:16px;margin:12px 0}"
  ".st{font-size:1.4em;font-weight:bold}.lbl{color:#666;font-size:.85em}"
  "a{color:#0070f3}nav{margin-bottom:16px}.hidden{display:none}"
  ".row{display:flex;gap:16px;flex-wrap:wrap}.col{flex:1;min-width:260px}"
  "#cam{width:100%;border-radius:6px;background:#222;min-height:180px;display:block}"
  ".bdg{display:inline-block;padding:2px 10px;border-radius:10px;font-size:.85em;font-weight:bold}"
  ".gr{background:#d4edda;color:#155724}.rd{background:#f8d7da;color:#721c24}"
  ".yl{background:#fff3cd;color:#856404}.no{background:#e2e3e5;color:#555}"
  ".kp{background:#cce5ff;color:#004085}.up{background:#ffd8a8;color:#7d4e00}"
  ".frl{list-style:none;padding:0;margin:8px 0}"
  ".frl li{padding:4px 0;border-bottom:1px solid #ddd;font-size:.9em}"
  "input[type=text]{width:100%;padding:7px;margin:6px 0;box-sizing:border-box;"
  "border:1px solid #ccc;border-radius:4px}"
  "button{padding:7px 14px;background:#0070f3;color:#fff;border:none;"
  "border-radius:4px;cursor:pointer;margin-right:4px}"
  "button:disabled{opacity:.5}.red{background:#dc3545}"
  "</style></head><body>"
  "<h2>Agent 1 &mdash; %AGENT%</h2>"
  "<nav>"
  "<a href='/settings'>Settings</a> | "
  "<a href='/log/door'>Door Log</a> | "
  "<a href='/log/face'>Face Log</a> | "
  "<a href='/log/alert'>Alert Log</a> | "
  "<a href='/logout'>Logout</a>"
  "</nav>"
  "<div class='row'>"
  "<div class='col'>"
  "<div class='card'><div class='lbl'>Alert Level</div><span class='bdg no' id='al'>&mdash;</span></div>"
  "<div class='card'><div class='lbl'>Door</div><div id='door'>&mdash;</div></div>"
  "<div class='card'><div class='lbl'>Agent 2</div><div id='a2'>&mdash;</div></div>"
  "<div class='card'><div class='lbl'>Last Known User</div><div id='lku'>&mdash;</div></div>"
  "<div class='card'><div class='lbl'>Uptime</div><div id='up'>&mdash;</div></div>"
  "<div class='card'><div class='lbl'>Hall Sensor</div><div id='hall'>&mdash;</div></div>"
  "</div>"
  "<div class='col'>"
  "<div class='card'><div class='lbl'>Camera Preview</div>"
  "<img id='cam' alt='stream'>"
  "<div style='margin-top:8px;display:flex;gap:16px;flex-wrap:wrap'>"
  "<div><div class='lbl'>Now</div><span id='raw' class='bdg no'>&mdash;</span></div>"
  "<div><div class='lbl'>Vote</div><span id='badge' class='bdg no'>&mdash;</span></div>"
  "</div></div>"
  "</div>"
  "</div>"
  "<div class='card'>"
  "<div class='lbl'>Face Recognition (<span id='fc'>0</span>/%MAX% enrolled)</div>"
  "<ul class='frl' id='fl'><li style='color:#999'>No faces enrolled</li></ul>"
  "<input type='text' id='fn' placeholder='Enter name to enroll' maxlength='16'>"
  "<button onclick='enr()' id='eb'>Enroll Face</button>"
  "<button onclick='clr()' class='red'>Clear All</button>"
  "<div id='msg' class='lbl' style='margin-top:4px'></div>"
  "</div>"
  "<div id='cv' style='display:none'>%CSRF%</div>"
  "<script>"
  "document.getElementById('cam').src='http://'+location.hostname+':81/stream';"
  "function fmt(ms){var s=Math.floor(ms/1000);"
  "return Math.floor(s/3600)+'h '+Math.floor(s%3600/60)+'m '+s%60+'s';}"
  "function poll(){"
  "fetch('/api/status').then(r=>r.json()).then(d=>{"
  "var al=document.getElementById('al');"
  "if(d.alert_level==='ALERT_RED'){al.className='bdg rd';al.textContent='RED'+(d.alarm_active?' ⚠️ ALARM':' ●');}"
  "else if(d.alert_level==='ALERT_YELLOW'){al.className='bdg yl';al.textContent='YELLOW ●';}"
  "else{al.className='bdg gr';al.textContent='GREEN ●';}"
  "document.getElementById('door').textContent=d.door_state||'?';"
  "var a2=document.getElementById('a2');"
  "a2.textContent=d.agent2_online?('Online · '+(d.presence_state||'?')):'Offline';"
  "a2.style.color=d.agent2_online?'#155724':'#721c24';"
  "document.getElementById('lku').textContent=d.last_known_user||'—';"
  "document.getElementById('up').textContent=fmt(d.uptime||0);"
  "document.getElementById('hall').textContent='raw: '+d.hall_raw+' / threshold: '+d.hall_threshold;"
  "if(d.face_count!==undefined)document.getElementById('fc').textContent=d.face_count;"
  "var r=document.getElementById('raw');"
  "if(d.face_result==='KNOWN'){r.className='bdg gr';"
  "r.textContent=(d.face_name||'Known')+(d.face_sim>0?' · '+d.face_sim.toFixed(3):'');}"
  "else if(d.face_result==='UNKNOWN'){r.className='bdg rd';"
  "r.textContent='Unknown'+(d.face_tex>0?' · tex:'+d.face_tex.toFixed(1):'');}"
  "else if(d.face_result==='DETECTED'){r.className='bdg no';r.textContent='Detected';}"
  "else{r.className='bdg no';r.textContent='—';}"
  "var b=document.getElementById('badge'),fv=d.face_voter_state;"
  "if(fv==='known_confirmed'){b.className='bdg gr';"
  "b.textContent=d.face_voter_confirmed_name||d.face_name||'Known';}"
  "else if(fv==='unknown_pending'){b.className='bdg up';"
  "b.textContent='Unknown… '+d.face_voter_unknown_elapsed_s+'s/'+d.face_voter_unknown_window_s+'s · '+d.face_voter_unknown_hits+' hits';}"
  "else if(fv==='known_pending'){b.className='bdg kp';"
  "b.textContent='Known? '+(d.face_name?d.face_name+' ':'')+d.face_voter_known_count+'/'+d.face_voter_known_min+' hits';}"
  "else if(d.face_result==='KNOWN'){b.className='bdg gr';b.textContent=d.face_name||'Known';}"
  "else if(d.face_result==='UNKNOWN'){b.className='bdg rd';b.textContent='Unknown';}"
  "else if(d.face_result==='DETECTED'){b.className='bdg no';b.textContent='Detected';}"
  "else{b.className='bdg no';b.textContent='—';}"
  "}).catch(()=>{});"
  "fetch('/api/face/list').then(r=>r.json()).then(d=>{"
  "var ul=document.getElementById('fl');ul.innerHTML='';"
  "if(d.faces&&d.faces.length){"
  "d.faces.forEach(function(n,i){"
  "var li=document.createElement('li');"
  "li.textContent=(i+1)+'. '+(n||'(unnamed)');ul.appendChild(li);});}"
  "else ul.innerHTML='<li style=\"color:#999\">No faces enrolled</li>';"
  "}).catch(()=>{});}"
  "function enr(){"
  "var n=document.getElementById('fn').value.trim();"
  "if(!n){document.getElementById('msg').textContent='Please enter a name.';return;}"
  "var c=document.getElementById('cv').textContent;"
  "document.getElementById('eb').disabled=true;"
  "document.getElementById('msg').textContent='Stand in front of camera...';"
  "fetch('/api/face/enroll',{method:'POST',"
  "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
  "body:'csrf='+encodeURIComponent(c)+'&name='+encodeURIComponent(n)})"
  ".then(r=>r.json()).then(function(d){"
  "document.getElementById('msg').textContent="
  "d.error?('Error: '+d.error):'Scheduled: \"'+n+'\" will be enrolled on next detection';"
  "document.getElementById('eb').disabled=false;"
  "document.getElementById('fn').value='';}).catch(function(){document.getElementById('eb').disabled=false;});}"
  "function clr(){"
  "if(!confirm('Clear all enrolled faces?'))return;"
  "var c=document.getElementById('cv').textContent;"
  "fetch('/api/face/clear',{method:'POST',"
  "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
  "body:'csrf='+encodeURIComponent(c)})"
  ".then(r=>r.json()).then(function(){"
  "document.getElementById('msg').textContent='All faces cleared.';"
  "document.getElementById('fl').innerHTML='<li style=\"color:#999\">No faces enrolled</li>';}).catch(()=>{});}"
  "poll();setInterval(poll,3000);"
  "</script></body></html>";

static const char SETTINGS_HTML[] =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'><title>Settings - Agent 1</title>"
  "<style>body{font-family:sans-serif;max-width:520px;margin:20px auto;padding:20px}"
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
  "<fieldset><legend>MQTT Broker</legend>"
  "<label>Broker IP / Hostname<br>"
  "<input name='mqtt_broker' maxlength='63' placeholder='192.168.x.x' value='%MQTT_BROKER%'></label>"
  "<label>Port<br>"
  "<input type='number' name='mqtt_port' min='1' max='65535' value='%MQTT_PORT%'></label>"
  "</fieldset>"
  "<fieldset><legend>Hall Sensor Threshold</legend>"
  "<label>Threshold (0-4095)<br>"
  "<input type='number' name='hall_threshold' min='0' max='4095' value='%HALL_THRESH%'></label>"
  "<p style='font-size:.8em;color:#666'>Current raw: %HALL_RAW% &mdash; "
  "set between closed and open readings.</p>"
  "</fieldset>"
  "<button>Save</button>"
  "</form></body></html>";

static const char PWCHANGE_HTML[] =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'><title>Set Password - Agent 1</title>"
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

static const char LOG_HTML[] =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'><title>%TITLE% - Agent 1</title>"
  "<style>body{font-family:sans-serif;max-width:900px;margin:20px auto;padding:20px}"
  "table{width:100%;border-collapse:collapse;font-size:.9em}"
  "th,td{padding:8px;text-align:left;border-bottom:1px solid #ddd}"
  "th{background:#f5f5f5;font-weight:bold}"
  "a{color:#0070f3}"
  "</style></head><body>"
  "<h2>%TITLE%</h2>"
  "<nav><a href='/dashboard'>&larr; Dashboard</a></nav>"
  "<div id='tbl'><p>Loading...</p></div>"
  "<script>"
  "function esc(v){return String(v===null||v===undefined?'':v)"
  ".replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/\"/g,'&quot;');}"
  "fetch('%API%').then(r=>r.json()).then(function(d){"
  "if(!d.length){document.getElementById('tbl').innerHTML='<p>No entries.</p>';return;}"
  "var keys=Object.keys(d[0]);"
  "var h='<table><tr>'+keys.map(k=>'<th>'+esc(k)+'</th>').join('')+'</tr>';"
  "d.forEach(function(row){"
  "h+='<tr>'+keys.map(k=>'<td>'+esc(row[k])+'</td>').join('')+'</tr>';});"
  "document.getElementById('tbl').innerHTML=h+'</table>';"
  "}).catch(function(){document.getElementById('tbl').innerHTML='<p>Failed to load.</p>';});"
  "</script></body></html>";

// ── Module-level state ────────────────────────────────────────────────────────

static SecurityStateMachine* _sm         = nullptr;
static const char*           _agentLabel = nullptr;
static FaceVoter*            _faceVoter  = nullptr;
static LogManager*           _logManager = nullptr;

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
                             SecurityStateMachine& sm,
                             const char* agentLabel,
                             FaceVoter* faceVoter,
                             LogManager* logManager) {
  _sm         = &sm;
  _agentLabel = agentLabel;
  _faceVoter  = faceVoter;
  _logManager = logManager;

  // Root → dashboard
  server.on("/", HTTP_GET, [&server]() {
    server.sendHeader("Location", "/dashboard");
    server.send(302, "text/plain", "");
  });

  server.on("/favicon.ico", HTTP_GET, [&server]() {
    server.send(204, "text/plain", "");
  });

  // ── Dashboard page ──────────────────────────────────────────────────────────
  server.on("/dashboard", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    String page = DASHBOARD_HTML;
    page.replace("%AGENT%", _agentLabel ? _agentLabel : "");
    page.replace("%CSRF%",  SessionAuth::getCsrfToken());
    page.replace("%MAX%",   String(FaceRecognizer::MAX_FACES));
    server.send(200, "text/html", page);
  });

  // ── Status API ──────────────────────────────────────────────────────────────
  server.on("/api/status", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;

    JsonDocument doc;
    doc["alert_level"]     = alertLevelToString(_sm->getAlertLevel());
    doc["door_state"]      = doorStateToString(_sm->getDoorState());
    doc["agent2_online"]   = _sm->isAgent2Online();
    doc["alarm_active"]    = _sm->isAlarmActive();
    doc["last_known_user"] = _sm->getLastKnownUser();
    doc["presence_state"]  = "";   // updated by AgentComm callback; reflected via sm
    doc["uptime"]          = millis();
    doc["hall_raw"]        = DoorSensor::getRaw();
    doc["hall_threshold"]  = SettingsStore::getHallThreshold();
    doc["face_count"]      = FaceRecognizer::count();
    doc["face_max"]        = FaceRecognizer::MAX_FACES;

    // Recognition result (only within FACE_RECENT_MS)
    FaceResult fr = FaceResult::NONE;
    if (CameraAgent::isInitialized()) {
      unsigned long sinceDetect = millis() - CameraAgent::lastDetectedMs();
      if (sinceDetect < FACE_RECENT_MS) {
        fr = CameraAgent::lastRawResult();
      }
    }
    doc["face_result"] = (fr == FaceResult::KNOWN)    ? "KNOWN"    :
                         (fr == FaceResult::UNKNOWN)   ? "UNKNOWN"  :
                         (fr == FaceResult::DETECTED)  ? "DETECTED" : "NONE";
    {
      const char* fn = FaceRecognizer::getLastMatchName();
      doc["face_name"] = fn ? fn : "";
    }
    doc["face_sim"] = FaceRecognizer::getLastSim();
    doc["face_tex"] = FaceRecognizer::getLastTex();

    // FaceVoter state
    if (_faceVoter) {
      FaceVoterStatus fvs = _faceVoter->getStatus(millis());
      const char* voterState;
      if      (fvs.knownConfirmed)   voterState = "known_confirmed";
      else if (!fvs.active)          voterState = "idle";
      else if (fvs.unknownHits > 0)  voterState = "unknown_pending";
      else if (fvs.knownCount > 0)   voterState = "known_pending";
      else                           voterState = "active";
      doc["face_voter_state"]             = voterState;
      doc["face_voter_confirmed_name"]    = fvs.confirmedName;
      doc["face_voter_known_count"]       = fvs.knownCount;
      doc["face_voter_known_min"]         = fvs.knownMin;
      doc["face_voter_unknown_hits"]      = fvs.unknownHits;
      doc["face_voter_unknown_elapsed_s"] = (int)(fvs.unknownElapsedMs / 1000UL);
      doc["face_voter_unknown_window_s"]  = (int)(fvs.unknownWindowMs  / 1000UL);
    }

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  // ── Log API ─────────────────────────────────────────────────────────────────
  server.on("/api/log/door", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    server.send(200, "application/json", _logManager ? _logManager->getDoorLogJson() : "[]");
  });

  server.on("/api/log/face", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    server.send(200, "application/json", _logManager ? _logManager->getFaceLogJson() : "[]");
  });

  server.on("/api/log/alert", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    server.send(200, "application/json", _logManager ? _logManager->getAlertLogJson() : "[]");
  });

  // ── Log pages ───────────────────────────────────────────────────────────────
  server.on("/log/door", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    String page = LOG_HTML;
    page.replace("%TITLE%", "Door Log");
    page.replace("%API%",   "/api/log/door");
    server.send(200, "text/html", page);
  });

  server.on("/log/face", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    String page = LOG_HTML;
    page.replace("%TITLE%", "Face Log");
    page.replace("%API%",   "/api/log/face");
    server.send(200, "text/html", page);
  });

  server.on("/log/alert", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    String page = LOG_HTML;
    page.replace("%TITLE%", "Alert Log");
    page.replace("%API%",   "/api/log/alert");
    server.send(200, "text/html", page);
  });

  // ── Settings page ───────────────────────────────────────────────────────────
  server.on("/settings", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    String page = SETTINGS_HTML;
    page.replace("%MSG%",         "");
    page.replace("%CSRF%",        SessionAuth::getCsrfToken());
    page.replace("%DISCORD_URL%", htmlAttrEscape(SettingsStore::getDiscordUrl()));
    page.replace("%MQTT_BROKER%", htmlAttrEscape(ConfigManager::getMqttBroker()));
    page.replace("%MQTT_PORT%",   String(ConfigManager::getMqttPort()));
    page.replace("%HALL_THRESH%", String(SettingsStore::getHallThreshold()));
    page.replace("%HALL_RAW%",    String(DoorSensor::getRaw()));
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

    // Password change
    String newPw     = server.arg("newpw");
    String confirmPw = server.arg("confirmpw");
    if (newPw.length() > 0) {
      if (newPw != confirmPw) {
        msg += "<p class='err'>Passwords do not match.</p>";
      } else if (!SettingsStore::setDashboardPassword(newPw)) {
        msg += "<p class='err'>Password must be 8-64 characters.</p>";
      } else {
        msg += "<p class='ok'>Password updated.</p>";
      }
    }

    // Discord URL
    if (server.hasArg("discord_url")) {
      String url = server.arg("discord_url");
      if (url.length() == 0) {
        SettingsStore::setDiscordUrl("");
      } else if (!SettingsStore::setDiscordUrl(url)) {
        msg += "<p class='err'>Invalid Discord URL.</p>";
      } else {
        msg += "<p class='ok'>Discord URL saved.</p>";
      }
    }

    // MQTT broker
    if (server.hasArg("mqtt_broker")) {
      String broker = server.arg("mqtt_broker");
      broker.trim();
      String portStr = server.arg("mqtt_port");
      uint16_t port  = (uint16_t)portStr.toInt();
      if (port == 0) port = MQTT_DEFAULT_PORT;
      if (!ConfigManager::save(broker, port)) {
        msg += "<p class='err'>Failed to save MQTT settings.</p>";
      } else {
        msg += "<p class='ok'>MQTT settings saved (restart to apply).</p>";
      }
    }

    // Hall threshold
    if (server.hasArg("hall_threshold")) {
      String hallArg = server.arg("hall_threshold");
      bool numeric   = hallArg.length() > 0;
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
          DoorSensor::setThreshold((uint16_t)val);
          msg += "<p class='ok'>Hall threshold saved.</p>";
        }
      }
    }

    String page = SETTINGS_HTML;
    page.replace("%MSG%",         msg);
    page.replace("%CSRF%",        SessionAuth::getCsrfToken());
    page.replace("%DISCORD_URL%", htmlAttrEscape(SettingsStore::getDiscordUrl()));
    page.replace("%MQTT_BROKER%", htmlAttrEscape(ConfigManager::getMqttBroker()));
    page.replace("%MQTT_PORT%",   String(ConfigManager::getMqttPort()));
    page.replace("%HALL_THRESH%", String(SettingsStore::getHallThreshold()));
    page.replace("%HALL_RAW%",    String(DoorSensor::getRaw()));
    server.send(200, "text/html", page);
  });

  // ── Password change pages ───────────────────────────────────────────────────
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
    String name = server.arg("name");
    name.trim();
    String sanitized;
    for (size_t i = 0; i < name.length() && (int)sanitized.length() < FaceRecognizer::MAX_NAME_LEN; i++) {
      char c = name[i];
      if (c >= 32 && c < 127) sanitized += c;
    }
    name = sanitized;
    CameraAgent::scheduleEnroll(name.length() > 0 ? name.c_str() : nullptr);
    JsonDocument doc;
    doc["scheduled"] = true;
    doc["count"]     = FaceRecognizer::count();
    doc["max"]       = FaceRecognizer::MAX_FACES;
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  server.on("/api/face/list", HTTP_GET, [&server]() {
    if (!requireAuthAndChangedPassword(server)) return;
    JsonDocument doc;
    JsonArray faces = doc["faces"].to<JsonArray>();
    for (int i = 0; i < FaceRecognizer::count(); i++) {
      const char* n = FaceRecognizer::getName(i);
      faces.add(n ? n : "");
    }
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
    CameraAgent::cancelEnroll();
    bool ok = FaceRecognizer::clearAll();
    JsonDocument doc;
    doc["cleared"] = ok;
    if (!ok) doc["error"] = "NVS write failed";
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
