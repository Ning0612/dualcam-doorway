#pragma once
#include <Arduino.h>
#include "esp_camera.h"

enum class RecognitionResult { KNOWN, UNKNOWN };

// Lightweight face recognizer using YUV422 block-luminance features.
// Extracts 32 floats (16 block means + 16 block stddevs, L2-normalized)
// from the central 60% of a YUV422 frame and compares via cosine similarity.
// Requires PSRAM (YUV422 mode). Falls back to UNKNOWN if frame is JPEG.
class FaceRecognizer {
public:
  static constexpr int MAX_FACES   = 7;
  static constexpr int FEATURE_DIM = 32;  // 16 means + 16 stddevs, unit-length

  static void begin();
  static bool enroll(camera_fb_t* fb);
  static RecognitionResult recognize(camera_fb_t* fb);
  static int  count() { return _n; }
  static bool clearAll();  // returns false if NVS persist fails
  static void setOnClearCallback(void(*cb)()) { _onClearCb = cb; }

private:
  static float _bank[MAX_FACES][FEATURE_DIM];
  static int   _n;
  static void(*_onClearCb)();

  static void  _extract(camera_fb_t* fb, float* out);
  static float _similarity(const float* a, const float* b);
  static bool  _persist();
  static void  _load();
};
