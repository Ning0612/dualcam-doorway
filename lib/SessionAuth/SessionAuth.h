#pragma once
#include <Arduino.h>
#include <WebServer.h>

class SessionAuth {
public:
  // Register /login (GET+POST) and /logout (GET) on server.
  // Must be called before server.begin().
  static void begin(WebServer& server);

  // Returns true if the request carries a valid, non-expired session cookie.
  // Resets TTL on success.
  static bool isAuthorized(WebServer& server);

  // Returns the per-boot CSRF token (used by HTML forms).
  static String getCsrfToken();

  // Returns true if token matches the current CSRF token.
  static bool verifyCsrf(const String& token);

private:
  static String        _sessionToken;
  static unsigned long _tokenCreatedMs;
  static int           _failCount;
  static unsigned long _lockoutUntilMs;
  static String        _csrfToken;

  static String _generateToken();
  static void   _clearSession();
  static bool   _checkPassword(const String& pw);
  static String _parseCookie(const String& header, const String& name);
};
