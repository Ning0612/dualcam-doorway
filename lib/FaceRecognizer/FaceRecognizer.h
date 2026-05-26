#pragma once
#include <Arduino.h>
#include "esp_camera.h"

enum class RecognitionResult { KNOWN, UNKNOWN, NO_FACE };

// Lightweight face recognizer using HOG-lite features on YUV422 (YUYV) frames.
// Extracts 64 floats (4×4 cells × 4 orientation bins, per-cell L1 + global L2
// normalized) from the central 60% of a YUV422 frame and compares via cosine
// similarity with per-user best-template matching and second-best margin check.
// Requires PSRAM (YUV422 mode). Returns NO_FACE for JPEG, invalid frames,
// low-gradient or sparse-active-cell scenes, or when no faces are enrolled.
class FaceRecognizer {
public:
  static constexpr int MAX_FACES             = 7;
  static constexpr int FEATURE_DIM           = 64;  // HOG-lite: 4×4 cells × 4 orientation bins
  static constexpr int MAX_NAME_LEN          = 16;  // max display name per user
  static constexpr int MAX_TEMPLATES_PER_USER = 5;  // templates per enrolled user

  static void begin();
  // Enroll: if name matches an existing user, adds a template to that user (up to
  // MAX_TEMPLATES_PER_USER). Otherwise creates a new user slot (up to MAX_FACES).
  static bool enroll(camera_fb_t* fb, const char* name = nullptr);
  static RecognitionResult recognize(camera_fb_t* fb);
  static int  count() { return _n; }
  static bool clearAll();  // returns false if NVS persist fails
  static void setOnClearCallback(void(*cb)()) { _onClearCb = cb; }

  // Returns true if enroll(fb, name) would not be rejected for capacity reasons.
  static bool canEnroll(const char* name = nullptr);

  // Returns name for user index, or nullptr if index is out of range or name is empty.
  static const char* getName(int index);
  // Returns number of enrolled templates for user index (0 if out of range).
  static int getTemplateCount(int index);
  // Returns name of the last recognize() match (KNOWN result), or nullptr.
  static const char* getLastMatchName();
  // Returns cosine similarity of the last KNOWN match (0 if last result was not KNOWN).
  static float getLastSim() { return _lastSim; }
  // Returns mean L1 gradient per pixel (HOG texture score) of the last recognize() call.
  static float getLastTex() { return _lastTex; }

private:
  static float   _bank[MAX_FACES][MAX_TEMPLATES_PER_USER][FEATURE_DIM];
  static uint8_t _tcnt[MAX_FACES];  // template count per user
  static char    _names[MAX_FACES][MAX_NAME_LEN + 1];
  static int     _n;
  static int     _lastMatchIdx;
  static float   _lastSim;  // similarity of last KNOWN match; 0 otherwise
  static float   _lastTex;  // texture score of last recognize() call
  static void(*_onClearCb)();

  // Returns mean L1 gradient per pixel as texture quality score (0 if sparse/rejected).
  static float _extract(camera_fb_t* fb, float* out);
  static float _similarity(const float* a, const float* b);
  static bool  _persist();
  static void  _load();
};
