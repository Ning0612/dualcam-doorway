#include "FaceRecognizer.h"
#include "config.h"
#include <Preferences.h>
#include <math.h>
#include <strings.h>  // strcasecmp

static const char* FR_NS    = "agent_cfg";
static const char* FR_FEAT  = "face_feat";
static const char* FR_COUNT = "face_cnt";
static const char* FR_NAMES = "face_names";
static const char* FR_TCNT  = "face_tcnt";  // per-user template counts (new in multi-template)

float   FaceRecognizer::_bank[FaceRecognizer::MAX_FACES][FaceRecognizer::MAX_TEMPLATES_PER_USER][FaceRecognizer::FEATURE_DIM];
uint8_t FaceRecognizer::_tcnt[FaceRecognizer::MAX_FACES];
char    FaceRecognizer::_names[FaceRecognizer::MAX_FACES][FaceRecognizer::MAX_NAME_LEN + 1];
int     FaceRecognizer::_n           = 0;
int     FaceRecognizer::_lastMatchIdx = -1;
float   FaceRecognizer::_lastSim      = 0.f;
float   FaceRecognizer::_lastTex      = 0.f;
void(*FaceRecognizer::_onClearCb)() = nullptr;

void FaceRecognizer::begin() {
  memset(_names, 0, sizeof(_names));
  memset(_tcnt,  0, sizeof(_tcnt));
  _load();
  Serial.printf("[FaceRecognizer] loaded %d/%d user(s)\n", _n, MAX_FACES);
}

// Extracts 32-dimensional feature from YUV422 (YUYV) frame.
// Divides central 60% region into 4×4 blocks; computes mean-Y and stddev-Y
// per block (16+16=32 floats), then L2-normalizes the vector.
// Caller must validate: fb->format==YUV422, fb->buf!=null, adequate fb->len.
// YUYV layout: Y at byte index (y*W*2 + x*2).
float FaceRecognizer::_extract(camera_fb_t* fb, float* out) {
  const int W  = (int)fb->width;
  const int H  = (int)fb->height;
  const int x0 = W / 5, x1 = W * 4 / 5;
  const int y0 = H / 5, y1 = H * 4 / 5;
  const int rw = x1 - x0, rh = y1 - y0;
  const uint8_t* buf = fb->buf;

  float textureSum = 0.f;

  for (int br = 0; br < 4; br++) {
    for (int bc = 0; bc < 4; bc++) {
      const int bx0 = x0 + bc * rw / 4;
      const int bx1 = x0 + (bc + 1) * rw / 4;
      const int by0 = y0 + br * rh / 4;
      const int by1 = y0 + (br + 1) * rh / 4;

      float sum = 0, sum2 = 0;
      int   n   = 0;
      for (int y = by0; y < by1; y++) {
        const int base = y * W * 2;
        for (int x = bx0; x < bx1; x++) {
          const float yv = buf[base + x * 2];
          sum  += yv;
          sum2 += yv * yv;
          n++;
        }
      }

      const float mean = (n > 0) ? sum / (float)n : 0.f;
      const float var  = (n > 0) ? (sum2 / (float)n - mean * mean) : 0.f;
      const float std  = (var > 0.f) ? sqrtf(var) : 0.f;

      out[br * 4 + bc]      = mean;
      out[16 + br * 4 + bc] = std;
      textureSum += std;
    }
  }

  // L2 normalize so cosine similarity equals dot product
  float norm = 0;
  for (int i = 0; i < FEATURE_DIM; i++) norm += out[i] * out[i];
  norm = sqrtf(norm);
  if (norm > 1e-6f) {
    for (int i = 0; i < FEATURE_DIM; i++) out[i] /= norm;
  }

  return textureSum / 16.0f;  // mean block stddev before normalization
}

float FaceRecognizer::_similarity(const float* a, const float* b) {
  float dot = 0;
  for (int i = 0; i < FEATURE_DIM; i++) dot += a[i] * b[i];
  return dot;
}

static bool _fbValid(camera_fb_t* fb) {
  if (!fb || !fb->buf) return false;
  if (fb->format != PIXFORMAT_YUV422) return false;
  if (fb->width == 0 || fb->height == 0) return false;
  // YUYV: 2 bytes per pixel
  if (fb->len < (size_t)fb->width * (size_t)fb->height * 2U) return false;
  return true;
}

bool FaceRecognizer::canEnroll(const char* name) {
  if (name && name[0]) {
    for (int i = 0; i < _n; i++) {
      if (_names[i][0] && strcasecmp(_names[i], name) == 0) {
        return _tcnt[i] < MAX_TEMPLATES_PER_USER;
      }
    }
  }
  return _n < MAX_FACES;
}

bool FaceRecognizer::enroll(camera_fb_t* fb, const char* name) {
  if (!_fbValid(fb)) {
    Serial.println("[FaceRecognizer] enroll rejected: invalid frame (format/size)");
    return false;
  }

  float feat[FEATURE_DIM];
  _extract(fb, feat);  // texture not checked on enroll — deliberate user action

  // If name matches an existing user, add a template to that user
  if (name && name[0]) {
    for (int i = 0; i < _n; i++) {
      if (_names[i][0] && strcasecmp(_names[i], name) == 0) {
        if (_tcnt[i] >= MAX_TEMPLATES_PER_USER) {
          Serial.printf("[FaceRecognizer] enroll rejected: template full for '%s' (%d/%d)\n",
                        name, _tcnt[i], MAX_TEMPLATES_PER_USER);
          return false;
        }
        memcpy(_bank[i][_tcnt[i]], feat, FEATURE_DIM * sizeof(float));
        _tcnt[i]++;
        if (!_persist()) {
          _tcnt[i]--;  // rollback: NVS write failed, revert in-memory count
          Serial.printf("[FaceRecognizer] template add failed: NVS write error for '%s'\n", name);
          return false;
        }
        Serial.printf("[FaceRecognizer] template %d/%d added for user '%s'\n",
                      _tcnt[i], MAX_TEMPLATES_PER_USER, name);
        return true;
      }
    }
  }

  // New user
  if (_n >= MAX_FACES) {
    Serial.printf("[FaceRecognizer] enroll rejected: user bank full (%d/%d)\n", _n, MAX_FACES);
    return false;
  }
  memcpy(_bank[_n][0], feat, FEATURE_DIM * sizeof(float));
  _tcnt[_n] = 1;
  if (name && name[0]) {
    strncpy(_names[_n], name, MAX_NAME_LEN);
    _names[_n][MAX_NAME_LEN] = '\0';
  } else {
    _names[_n][0] = '\0';
  }
  _n++;
  if (!_persist()) {
    _n--;
    _tcnt[_n] = 0;
    memset(_names[_n], 0, MAX_NAME_LEN + 1);
    _persist();  // best-effort sync; failure logged inside _persist()
    Serial.println("[FaceRecognizer] enroll failed: NVS write error (rolled back)");
    return false;
  }
  Serial.printf("[FaceRecognizer] user %d/%d enrolled as '%s' (1/%d templates)\n",
                _n, MAX_FACES, _names[_n - 1], MAX_TEMPLATES_PER_USER);
  return true;
}

RecognitionResult FaceRecognizer::recognize(camera_fb_t* fb) {
  if (!_fbValid(fb) || _n == 0) {
    _lastMatchIdx = -1; _lastSim = 0.f; _lastTex = 0.f;
    return RecognitionResult::NO_FACE;
  }

  float feat[FEATURE_DIM];
  float texture = _extract(fb, feat);
  _lastTex = texture;

  if (texture < FACE_TEXTURE_MIN_STDDEV) {
    Serial.printf("[FaceRecognizer] reject: flat scene (texture=%.1f)\n", texture);
    _lastMatchIdx = -1; _lastSim = 0.f;
    return RecognitionResult::NO_FACE;
  }

  // For each user, find the best-matching template; then pick the best user.
  // secondScore tracks the second-best user score for margin check.
  float bestScore   = -1.f, secondScore = -1.f;
  int   bestUser    = -1;

  for (int u = 0; u < _n; u++) {
    float userBest = -1.f;
    for (int t = 0; t < (int)_tcnt[u]; t++) {
      float s = _similarity(feat, _bank[u][t]);
      if (s > userBest) userBest = s;
    }
    if (userBest > bestScore) {
      secondScore = bestScore;
      bestScore   = userBest;
      bestUser    = u;
    } else if (userBest > secondScore) {
      secondScore = userBest;
    }
  }

  // When only one user is enrolled, secondScore stays -1 → margin is effectively
  // infinite so the single-user case always passes the margin check.
  const float margin = (secondScore >= 0.f) ? (bestScore - secondScore) : 1.f;

  if (bestScore >= FACE_SIMILARITY_THRESHOLD && margin >= FACE_MARGIN_MIN) {
    _lastMatchIdx = bestUser;
    _lastSim      = bestScore;
    Serial.printf("[FaceRecognizer] match user %d '%s' (sim=%.3f, margin=%.3f, tex=%.1f)\n",
                  bestUser, _names[bestUser], bestScore, margin, texture);
    return RecognitionResult::KNOWN;
  }

  _lastMatchIdx = -1; _lastSim = 0.f;
  static unsigned long _lastUnknownLogMs = 0;
  unsigned long nowMs = millis();
  if (nowMs - _lastUnknownLogMs >= 10000UL) {
    _lastUnknownLogMs = nowMs;
    Serial.printf("[FaceRecognizer] UNKNOWN (best=%.3f, margin=%.3f, tex=%.1f)\n",
                  bestScore, margin, texture);
  }
  return RecognitionResult::UNKNOWN;
}

const char* FaceRecognizer::getName(int idx) {
  if (idx < 0 || idx >= _n) return nullptr;
  return _names[idx][0] ? _names[idx] : nullptr;
}

int FaceRecognizer::getTemplateCount(int idx) {
  if (idx < 0 || idx >= _n) return 0;
  return (int)_tcnt[idx];
}

const char* FaceRecognizer::getLastMatchName() {
  return getName(_lastMatchIdx);
}

bool FaceRecognizer::clearAll() {
  _n = 0;
  _lastMatchIdx = -1;
  memset(_names, 0, sizeof(_names));
  memset(_tcnt,  0, sizeof(_tcnt));
  if (_onClearCb) _onClearCb();
  if (!_persist()) {
    Serial.println("[FaceRecognizer] WARNING: NVS clear failed — runtime cleared but reboot may restore old data");
    return false;
  }
  Serial.println("[FaceRecognizer] all faces cleared");
  return true;
}

// NVS layout (new multi-template format):
//   face_cnt  : uint8  — number of enrolled users
//   face_feat : blob   — _n × MAX_TEMPLATES_PER_USER × FEATURE_DIM × float32
//   face_tcnt : blob   — _n × uint8 (template count per user)
//   face_names: blob   — _n × (MAX_NAME_LEN+1) chars
// Old single-template blobs (face_feat size = _n × FEATURE_DIM × 4) are detected
// by size mismatch in _load() and auto-cleared, requiring re-enrollment.
bool FaceRecognizer::_persist() {
  Preferences prefs;
  if (!prefs.begin(FR_NS, false)) {
    Serial.println("[FaceRecognizer] NVS open failed — faces not persisted");
    return false;
  }
  bool ok = true;
  if (prefs.putUChar(FR_COUNT, (uint8_t)_n) == 0) ok = false;
  if (_n > 0) {
    const size_t fBytes = (size_t)_n * MAX_TEMPLATES_PER_USER * FEATURE_DIM * sizeof(float);
    if (prefs.putBytes(FR_FEAT,  _bank,  fBytes) != fBytes) ok = false;
    const size_t tBytes = (size_t)_n * sizeof(uint8_t);
    if (prefs.putBytes(FR_TCNT,  _tcnt,  tBytes) != tBytes) ok = false;
    const size_t nBytes = (size_t)_n * (MAX_NAME_LEN + 1);
    if (prefs.putBytes(FR_NAMES, _names, nBytes) != nBytes) ok = false;
  } else {
    prefs.remove(FR_FEAT);
    prefs.remove(FR_TCNT);
    prefs.remove(FR_NAMES);
  }
  prefs.end();
  return ok;
}

void FaceRecognizer::_load() {
  Preferences prefs;
  if (!prefs.begin(FR_NS, true)) return;
  const uint8_t cnt = prefs.getUChar(FR_COUNT, 0);
  bool corrupted = false;
  if (cnt > 0 && cnt <= MAX_FACES) {
    const size_t fExpected = (size_t)cnt * MAX_TEMPLATES_PER_USER * FEATURE_DIM * sizeof(float);
    const size_t got = prefs.getBytes(FR_FEAT, _bank, fExpected);
    if (got == fExpected) {
      _n = (int)cnt;
      // Load template counts; if key absent (old firmware), default to 1 per user.
      const size_t tExpected = (size_t)cnt * sizeof(uint8_t);
      if (prefs.getBytes(FR_TCNT, _tcnt, tExpected) != tExpected) {
        for (int i = 0; i < _n; i++) _tcnt[i] = 1;
      }
      // Clamp to valid range in case of partial corruption
      for (int i = 0; i < _n; i++) {
        if (_tcnt[i] == 0 || _tcnt[i] > MAX_TEMPLATES_PER_USER) _tcnt[i] = 1;
      }
      // Load names; force-NUL-terminate every slot to guard against corrupt blobs.
      const size_t nExpected = (size_t)cnt * (MAX_NAME_LEN + 1);
      prefs.getBytes(FR_NAMES, _names, nExpected);
      for (int i = 0; i < _n; i++) _names[i][MAX_NAME_LEN] = '\0';
    } else {
      // Size mismatch: likely old single-template format. Clear and require re-enrollment.
      Serial.println("[FaceRecognizer] NVS data size mismatch (old format?) — clearing stored faces");
      _n        = 0;
      corrupted = true;
    }
  }
  prefs.end();

  // Auto-clean: remove corrupt entry so it doesn't repeat on every boot
  if (corrupted) _persist();
}
