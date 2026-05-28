#include "SettingsStore.h"
#include "config.h"
#include <Preferences.h>
#include <mbedtls/sha256.h>

static const char* NVS_NS    = "agent_cfg";
static const char* PW_HASH   = "dashboard_pw";
static const char* PW_CHGD   = "pw_changed";
static const char* DISC_URL  = "discord_url";
static const char* HALL_LO_KEY = "hall_lo";
static const char* HALL_HI_KEY = "hall_hi";
static const char* SALT      = "faceguard_s2024";

void SettingsStore::init() {
  // NVS namespace is created lazily on first write; nothing to do here.
}

bool SettingsStore::hasDefaultPassword() {
  Preferences prefs;
  prefs.begin(NVS_NS, true);
  bool changed = prefs.getBool(PW_CHGD, false);
  prefs.end();
  return !changed;
}

String SettingsStore::getDashboardPwHash() {
  Preferences prefs;
  prefs.begin(NVS_NS, true);
  String h = prefs.getString(PW_HASH, "");
  prefs.end();
  return h;
}

bool SettingsStore::setDashboardPassword(const String& newPw) {
  if (newPw.length() < 8 || newPw.length() > 64) return false;
  bool hasNonSpace = false;
  for (size_t i = 0; i < newPw.length(); i++) {
    unsigned char c = (unsigned char)newPw[i];
    if (c < 0x20 || c > 0x7E) return false;
    if (c != ' ') hasNonSpace = true;
  }
  if (!hasNonSpace) return false;

  String hashed = hashPassword(newPw);

  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) {
    Serial.println("[SettingsStore] ERROR: NVS begin() failed.");
    return false;
  }
  prefs.putString(PW_HASH, hashed);
  prefs.putBool(PW_CHGD, true);
  prefs.end();
  return true;
}

String SettingsStore::getDiscordUrl() {
  Preferences prefs;
  prefs.begin(NVS_NS, true);
  String url = prefs.getString(DISC_URL, "");
  prefs.end();
  return url;
}

bool SettingsStore::setDiscordUrl(const String& url) {
  if (url.length() == 0) {
    Preferences prefs;
    if (!prefs.begin(NVS_NS, false)) {
      Serial.println("[SettingsStore] ERROR: NVS begin() failed.");
      return false;
    }
    prefs.putString(DISC_URL, "");
    prefs.end();
    return true;
  }
  if (url.length() > 256 || !_isValidDiscordUrl(url)) return false;

  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) {
    Serial.println("[SettingsStore] ERROR: NVS begin() failed.");
    return false;
  }
  prefs.putString(DISC_URL, url);
  prefs.end();
  return true;
}

String SettingsStore::hashPassword(const String& pw) {
  String salted = String(SALT) + pw;

  uint8_t hash[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);  // 0 = SHA-256 (not SHA-224)
  mbedtls_sha256_update(&ctx,
    reinterpret_cast<const unsigned char*>(salted.c_str()), salted.length());
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);

  char buf[65];
  for (int i = 0; i < 32; i++) sprintf(buf + i * 2, "%02x", hash[i]);
  buf[64] = '\0';
  return String(buf);
}

uint16_t SettingsStore::getHallLowerBound() {
  Preferences prefs;
  prefs.begin(NVS_NS, true);
  uint16_t v = (uint16_t)prefs.getUInt(HALL_LO_KEY, HALL_DEFAULT_LOWER);
  prefs.end();
  return v;
}

uint16_t SettingsStore::getHallUpperBound() {
  Preferences prefs;
  prefs.begin(NVS_NS, true);
  uint16_t v = (uint16_t)prefs.getUInt(HALL_HI_KEY, HALL_DEFAULT_UPPER);
  prefs.end();
  return v;
}

bool SettingsStore::setHallBounds(uint16_t lower, uint16_t upper) {
  if (lower >= upper) return false;
  if ((int)upper - (int)lower <= 2 * (int)HALL_HYSTERESIS) return false;
  if (upper > 4095) return false;
  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) {
    Serial.println("[SettingsStore] ERROR: NVS begin() failed.");
    return false;
  }
  bool ok = (prefs.putUInt(HALL_LO_KEY, lower) > 0) &&
            (prefs.putUInt(HALL_HI_KEY,  upper) > 0);
  prefs.end();
  return ok;
}

bool SettingsStore::_isValidDiscordUrl(const String& url) {
  return url.startsWith("https://discord.com/api/webhooks/") ||
         url.startsWith("https://discordapp.com/api/webhooks/");
}
