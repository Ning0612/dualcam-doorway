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

// Extracts 64-dimensional HOG-lite feature from YUV422 (YUYV) frame.
//
// Pipeline:
//   Central 60% ROI → 4×4 grid of cells → per-cell 4-bin gradient histogram
//   → per-cell L1 normalize → global L2 normalize → 64-float unit vector
//
// Orientation bins (unsigned, no atan2):
//   ax = |dx|, ay = |dy|, sign-agreement = (dx ^ dy) >= 0
//   ax >= ay → horizontal edge:  bin 0 (same-sign) / bin 1 (diff-sign)
//   ay >  ax → vertical edge:    bin 2 (same-sign) / bin 3 (diff-sign)
//
// Returns mean L1 gradient per pixel as texture score (see FACE_TEXTURE_MIN_STDDEV).
// YUYV layout: Y at byte index (y*W*2 + x*2).
float FaceRecognizer::_extract(camera_fb_t* fb, float* out) {
  const int W  = (int)fb->width;
  const int H  = (int)fb->height;
  const int x0 = W / 5, x1 = W * 4 / 5;
  const int y0 = H / 5, y1 = H * 4 / 5;
  const int rw = x1 - x0, rh = y1 - y0;
  const uint8_t* buf = fb->buf;

  memset(out, 0, FEATURE_DIM * sizeof(float));

  float totalEnergy = 0.f;
  int   totalPix    = 0;
  int   activeCells = 0;  // cells with mean gradient per pixel > 0.5 (Codex finding #2)

  for (int cr = 0; cr < 4; cr++) {
    for (int cc = 0; cc < 4; cc++) {
      const int bx0 = x0 + cc * rw / 4;
      const int bx1 = x0 + (cc + 1) * rw / 4;
      const int by0 = y0 + cr * rh / 4;
      const int by1 = y0 + (cr + 1) * rh / 4;

      float hist[4]    = {};
      float cellEnergy = 0.f;
      int   cellPix    = 0;

      for (int y = by0; y < by1; y++) {
        if (y < 1 || y >= H - 1) continue;
        const uint8_t* row  = buf + y       * W * 2;
        const uint8_t* rowP = buf + (y - 1) * W * 2;
        const uint8_t* rowN = buf + (y + 1) * W * 2;

        for (int x = bx0; x < bx1; x++) {
          if (x < 1 || x >= W - 1) continue;
          cellPix++;

          const int16_t dx = (int16_t)row[(x + 1) * 2] - (int16_t)row[(x - 1) * 2];
          const int16_t dy = (int16_t)rowN[x * 2]      - (int16_t)rowP[x * 2];
          const int16_t ax = (dx >= 0) ? dx : -dx;
          const int16_t ay = (dy >= 0) ? dy : -dy;
          const float   mag = (float)(ax + ay);

          if (mag < 2.f) continue;  // ignore near-zero gradients (noise floor)

          // Unsigned orientation: explicit sign comparison (readable, no bitwise trick)
          const bool sameSign = (dx >= 0) == (dy >= 0);
          const int  bin      = (ax >= ay) ? (sameSign ? 0 : 1) : (sameSign ? 2 : 3);
          hist[bin] += mag;
          cellEnergy += mag;
        }
      }

      // Per-cell L1 normalize → removes absolute brightness / contrast variation.
      // Cells with negligible gradient stay zero (treated as featureless).
      if (cellEnergy > 1.f) {
        for (int b = 0; b < 4; b++) hist[b] /= cellEnergy;
        // Cell is "active" if mean gradient per pixel exceeds noise floor
        if (cellPix > 0 && (cellEnergy / (float)cellPix) > 0.5f) activeCells++;
      }

      const int base_idx = (cr * 4 + cc) * 4;
      for (int b = 0; b < 4; b++) out[base_idx + b] = hist[b];

      totalEnergy += cellEnergy;
      totalPix    += cellPix;
    }
  }

  // Active-cell gate: reject frames where gradient is too sparse or concentrated
  // in too few cells (e.g. single door-frame edge, shadow stripe, hair sliver).
  // Require at least 4 of 16 cells to be active before proceeding to match.
  if (activeCells < 4) return 0.f;

  // Global L2 normalize so cosine similarity = dot product
  float norm = 0.f;
  for (int i = 0; i < FEATURE_DIM; i++) norm += out[i] * out[i];
  norm = sqrtf(norm);
  if (norm > 1e-6f) {
    for (int i = 0; i < FEATURE_DIM; i++) out[i] /= norm;
  }

  // Texture score: mean L1 gradient per pixel (blank wall → ~0, face → ~2–8)
  return (totalPix > 0) ? (totalEnergy / (float)totalPix) : 0.f;
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
  if (cnt > (uint8_t)MAX_FACES) {
    Serial.printf("[FaceRecognizer] NVS face_cnt=%d out of range — clearing\n", cnt);
    corrupted = true;
  } else if (cnt > 0) {
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
