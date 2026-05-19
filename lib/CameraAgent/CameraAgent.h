#pragma once
#include <Arduino.h>
#include <WiFiServer.h>
#include <WiFiClient.h>

enum class FaceResult { NONE, DETECTED, CAMERA_ERROR };

class CameraAgent {
public:
  // Call once in setup(), after WiFi is connected
  static bool begin();

  // Call every loop(); internally rate-limited to CAMERA_DETECT_INTERVAL_MS.
  // Returns DETECTED only on the first tick where a face appears (edge trigger).
  static FaceResult tick();

  // Starts MJPEG stream server on port 81 via a dedicated FreeRTOS task.
  // Call once in setup() after begin(). The task runs independently of loop().
  static void startStreamServer();

  // No-op: streaming is managed by the internal FreeRTOS task.
  // Kept for API compatibility; safe to call or omit from loop().
  static void handleStreamClients() {}

  static bool          isInitialized()  { return _ok; }
  static unsigned long lastDetectedMs() { return _lastDetMs; }

private:
  static bool          _ok;
  static bool          _hasPsram;
  static unsigned long _lastDetMs;
  static unsigned long _nextDetMs;
  static FaceResult    _prevResult;
  static FaceResult    _lastResult;
  static WiFiServer*   _streamSrv;
  static TaskHandle_t  _streamTask;

  static FaceResult _runDetection();
  static void       _streamTaskFn(void* arg);
};
