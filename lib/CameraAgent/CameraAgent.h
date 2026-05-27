#pragma once
#include <Arduino.h>
#include <WiFiServer.h>
#include <WiFiClient.h>

enum class FaceResult { NONE, DETECTED, KNOWN, UNKNOWN, CAMERA_ERROR };
// DETECTED : face found, no enrolled faces to compare against (Phase 4 compat)
// KNOWN    : face matches an enrolled identity
// UNKNOWN  : face detected but does not match any enrolled identity

class CameraAgent {
public:
  // Call once in setup(), after WiFi is connected
  static bool begin();

  // Call every loop(); internally rate-limited to CAMERA_DETECT_INTERVAL_MS.
  // Returns DETECTED only on the first tick where a face appears (edge trigger).
  static FaceResult tick();

  // Flag the next detected face for enrollment instead of recognition.
  // Enrollment is automatically cancelled after CAMERA_ENROLL_TIMEOUT_MS if no face appears.
  // Safe to call from loop() or a route handler (core-1 only).
  static void scheduleEnroll(const char* name = nullptr);
  static void cancelEnroll();

  // Starts MJPEG stream server on port 81 via a dedicated FreeRTOS task.
  // Call once in setup() after begin(). The task runs independently of loop().
  static void startStreamServer();

  // No-op: streaming is managed by the internal FreeRTOS task.
  // Kept for API compatibility; safe to call or omit from loop().
  static void handleStreamClients() {}

  // Capture a single frame as JPEG. Caller must free(*buf) after use.
  // Returns false if camera is not ready or conversion fails.
  static bool captureJpeg(uint8_t** buf, size_t* len);

  static bool          isInitialized()  { return _ok; }
  static unsigned long lastDetectedMs() { return _lastDetMs; }
  static FaceResult    lastRawResult()   { return _lastResult; }
  static unsigned long lastRawResultMs() { return _lastRunMs; }

private:
  static bool          _ok;
  static bool          _hasPsram;
  static unsigned long _lastDetMs;
  static unsigned long _lastRunMs;
  static unsigned long _nextDetMs;
  static FaceResult    _prevResult;
  static FaceResult    _lastResult;
  static WiFiServer*   _streamSrv;
  static TaskHandle_t  _streamTask;

  static bool          _enrollNext;
  static unsigned long _enrollExpireMs;
  static char          _enrollName[17];  // max 16 chars + null, matches FaceRecognizer::MAX_NAME_LEN
  static FaceResult _runDetection();
  static void       _streamTaskFn(void* arg);
};
