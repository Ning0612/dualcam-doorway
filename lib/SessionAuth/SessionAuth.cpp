#include "SessionAuth.h"
#include "SettingsStore.h"
#include "config.h"
#include <esp_random.h>

// Static member definitions
String        SessionAuth::_sessionToken   = "";
unsigned long SessionAuth::_tokenCreatedMs = 0;
int           SessionAuth::_failCount      = 0;
unsigned long SessionAuth::_lockoutUntilMs = 0;
String        SessionAuth::_csrfToken      = "";

static const char LOGIN_HTML[] =
  "<!DOCTYPE html><html><head>"
  "<meta charset='utf-8'><title>Login - FaceGuard</title>"
  "<style>body{font-family:sans-serif;max-width:360px;margin:60px auto;padding:20px}"
  "input{width:100%;padding:8px;margin:6px 0;box-sizing:border-box}"
  "button{width:100%;padding:10px;background:#0070f3;color:#fff;border:none;cursor:pointer}"
  ".err{color:red;font-size:.9em}</style></head><body>"
  "<h2>FaceGuard Dashboard</h2>"
  "%ERR%"
  "<form method='POST' action='/login'>"
  "<label>Password<br><input type='password' name='pw' required autofocus></label>"
  "<button>Login</button>"
  "</form></body></html>";

void SessionAuth::begin(WebServer& server) {
  _csrfToken = _generateToken();

  // Tell WebServer to collect the Cookie header
  static const char* keys[] = {"Cookie"};
  server.collectHeaders(keys, 1);

  server.on("/login", HTTP_GET, [&server]() {
    String page = LOGIN_HTML;
    page.replace("%ERR%", "");
    server.send(200, "text/html", page);
  });

  server.on("/login", HTTP_POST, [&server]() {
    if (millis() < _lockoutUntilMs) {
      String page = LOGIN_HTML;
      page.replace("%ERR%", "<p class='err'>Too many failed attempts. Please wait.</p>");
      server.send(403, "text/html", page);
      return;
    }

    String pw = server.arg("pw");
    if (_checkPassword(pw)) {
      _failCount      = 0;
      _sessionToken   = _generateToken();
      _tokenCreatedMs = millis();

      String cookie = "sid=" + _sessionToken + "; HttpOnly; Path=/; SameSite=Lax";
      server.sendHeader("Set-Cookie", cookie);

      if (SettingsStore::hasDefaultPassword()) {
        server.sendHeader("Location", "/password/change");
      } else {
        server.sendHeader("Location", "/dashboard");
      }
      server.send(302, "text/plain", "");
    } else {
      _failCount++;
      if (_failCount >= LOGIN_MAX_FAILS) {
        _lockoutUntilMs = millis() + LOGIN_LOCKOUT_MS;
        _failCount      = 0;
      }
      String page = LOGIN_HTML;
      page.replace("%ERR%", "<p class='err'>Invalid password.</p>");
      server.send(401, "text/html", page);
    }
  });

  server.on("/logout", HTTP_GET, [&server]() {
    _clearSession();
    server.sendHeader("Set-Cookie", "sid=; HttpOnly; Path=/; Max-Age=0");
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "");
  });
}

bool SessionAuth::isAuthorized(WebServer& server) {
  if (_sessionToken.length() == 0) return false;

  if (millis() - _tokenCreatedMs > DASHBOARD_SESSION_TTL_MS) {
    _clearSession();
    return false;
  }

  String sid = _parseCookie(server.header("Cookie"), "sid");
  if (sid == _sessionToken) {
    _tokenCreatedMs = millis();  // reset TTL on each authorized request
    return true;
  }
  return false;
}

String SessionAuth::getCsrfToken() {
  return _csrfToken;
}

bool SessionAuth::verifyCsrf(const String& token) {
  return token.length() > 0 && token == _csrfToken;
}

String SessionAuth::_generateToken() {
  String token;
  char   buf[3];
  for (int i = 0; i < 16; i++) {
    sprintf(buf, "%02x", static_cast<uint8_t>(esp_random() & 0xFF));
    token += buf;
  }
  return token;
}

void SessionAuth::_clearSession() {
  _sessionToken   = "";
  _tokenCreatedMs = 0;
}

bool SessionAuth::_checkPassword(const String& pw) {
  if (SettingsStore::hasDefaultPassword()) {
    return pw == "admin";
  }
  String stored = SettingsStore::getDashboardPwHash();
  return stored.length() > 0 && stored == SettingsStore::hashPassword(pw);
}

String SessionAuth::_parseCookie(const String& header, const String& name) {
  String prefix = name + "=";
  int    start  = 0;
  while (start < (int)header.length()) {
    int semi = header.indexOf(';', start);
    if (semi < 0) semi = header.length();
    String pair = header.substring(start, semi);
    // trim leading spaces
    int ws = 0;
    while (ws < (int)pair.length() && pair[ws] == ' ') ws++;
    pair = pair.substring(ws);
    if (pair.startsWith(prefix)) {
      return pair.substring(prefix.length());
    }
    start = semi + 1;
  }
  return "";
}
