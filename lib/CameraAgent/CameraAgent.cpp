#include "CameraAgent.h"
#include "config.h"
#include "esp_camera.h"
#include "img_converters.h"

// ── AI Thinker / NMK99 pin map ────────────────────────────────────────────────
// GPIO 32 is camera PWDN — must NOT be used as PIN_BUZZER when camera is active.
// GPIO 34 is CAM_D6 — must NOT be used as PIN_DOOR when camera is active.
#define CAM_PWDN   32
#define CAM_RESET  -1
#define CAM_XCLK    0
#define CAM_SIOD   26
#define CAM_SIOC   27
#define CAM_D7     35
#define CAM_D6     34
#define CAM_D5     39
#define CAM_D4     36
#define CAM_D3     21
#define CAM_D2     19
#define CAM_D1     18
#define CAM_D0      5
#define CAM_VSYNC  25
#define CAM_HREF   23
#define CAM_PCLK   22

// ── Statics ───────────────────────────────────────────────────────────────────
bool          CameraAgent::_ok         = false;
bool          CameraAgent::_hasPsram   = false;
unsigned long CameraAgent::_lastDetMs  = 0;
unsigned long CameraAgent::_nextDetMs  = 0;
FaceResult    CameraAgent::_prevResult = FaceResult::NONE;
FaceResult    CameraAgent::_lastResult = FaceResult::NONE;
WiFiServer*   CameraAgent::_streamSrv  = nullptr;
TaskHandle_t  CameraAgent::_streamTask = nullptr;

// ── MJPEG stream constants ────────────────────────────────────────────────────
static const char* STREAM_CT  = "multipart/x-mixed-replace;boundary=frame";
static const char* STREAM_HDR = "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n";
static const char* STREAM_END = "\r\n--frame--\r\n";

// ── begin() ───────────────────────────────────────────────────────────────────

bool CameraAgent::begin() {
  _hasPsram = psramFound();

  camera_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));  // zero-init to avoid undefined fields in newer SDK versions

  cfg.pin_pwdn     = CAM_PWDN;
  cfg.pin_reset    = CAM_RESET;
  cfg.pin_xclk     = CAM_XCLK;
  cfg.pin_sccb_sda = CAM_SIOD;
  cfg.pin_sccb_scl = CAM_SIOC;
  cfg.pin_d7       = CAM_D7;
  cfg.pin_d6       = CAM_D6;
  cfg.pin_d5       = CAM_D5;
  cfg.pin_d4       = CAM_D4;
  cfg.pin_d3       = CAM_D3;
  cfg.pin_d2       = CAM_D2;
  cfg.pin_d1       = CAM_D1;
  cfg.pin_d0       = CAM_D0;
  cfg.pin_vsync    = CAM_VSYNC;
  cfg.pin_href     = CAM_HREF;
  cfg.pin_pclk     = CAM_PCLK;
  cfg.xclk_freq_hz = 20000000;
  cfg.ledc_timer   = LEDC_TIMER_0;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.frame_size   = FRAMESIZE_QVGA;  // 320x240

  if (_hasPsram) {
    // YUV422 enables full skin-color face detection; PSRAM holds the 153 KB frame.
    // Two frame buffers allow detection and streaming to proceed without contention.
    cfg.pixel_format = PIXFORMAT_YUV422;
    cfg.fb_count     = 2;
    cfg.jpeg_quality = 10;
    Serial.println("[Camera] PSRAM found — YUV422 mode, face detection enabled");
  } else {
    // JPEG-only: stream available but face detection is disabled (insufficient heap).
    cfg.pixel_format = PIXFORMAT_JPEG;
    cfg.fb_count     = 1;
    cfg.jpeg_quality = 12;
    Serial.println("[Camera] WARNING: no PSRAM detected — JPEG mode only, face detection DISABLED");
    Serial.println("[Camera] If board has PSRAM, verify -DBOARD_HAS_PSRAM and PSRAM wiring.");
  }

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("[Camera] init failed: 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_vflip(s, 1);
    s->set_hmirror(s, 0);
    s->set_brightness(s, 1);
    s->set_saturation(s, 0);
  }

  _ok = true;
  Serial.println("[Camera] initialized OK");
  return true;
}

// ── Face detection (YCbCr skin-color segmentation) ───────────────────────────
//
// YUV422 (YUYV) layout: Y0 U Y1 V Y2 U Y3 V ...  (2 bytes per pixel, shared chroma)
// Skin thresholds in YCbCr (ITU-R BT.601, components 0–255):
//   Cb (U): 77–127   Cr (V): 133–173
// Source: Kovac et al., "Human Skin Color Clustering for Face Detection", 2003.
//
// Detection region: central 60% of frame to reduce background noise.
// Threshold: >= SKIN_PIXEL_THRESHOLD skin pixels in that region.

#define SKIN_PIXEL_THRESHOLD  800

static inline bool isSkin(uint8_t cb, uint8_t cr) {
  return (cb >= 77 && cb <= 127) && (cr >= 133 && cr <= 173);
}

FaceResult CameraAgent::_runDetection() {
  if (!_ok || !_hasPsram) return FaceResult::NONE;

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[Camera] frame capture failed");
    return FaceResult::CAMERA_ERROR;
  }

  if (fb->format != PIXFORMAT_YUV422 || fb->width == 0 || fb->height == 0) {
    esp_camera_fb_return(fb);
    return FaceResult::NONE;
  }

  int w  = (int)fb->width;
  int h  = (int)fb->height;
  int x0 = w / 5;
  int x1 = w * 4 / 5;
  int y0 = h / 5;
  int y1 = h * 4 / 5;

  uint32_t       skinCount = 0;
  const uint8_t* buf       = fb->buf;

  for (int y = y0; y < y1; y++) {
    int rowBase = y * w * 2;  // 2 bytes per pixel in YUYV
    for (int x = x0; x < x1; x += 2) {
      int     i  = rowBase + x * 2;
      uint8_t cb = buf[i + 1];  // U (Cb)
      uint8_t cr = buf[i + 3];  // V (Cr)
      if (isSkin(cb, cr)) skinCount++;
    }
  }

  esp_camera_fb_return(fb);

  if (skinCount >= SKIN_PIXEL_THRESHOLD) {
    Serial.printf("[Camera] face detected (skin pixels: %u)\n", skinCount);
    return FaceResult::DETECTED;
  }
  return FaceResult::NONE;
}

// ── tick() ────────────────────────────────────────────────────────────────────
// Returns DETECTED only on the rising edge (NONE → DETECTED transition).
// This prevents the state machine from receiving repeated events while a face
// remains continuously in frame.

FaceResult CameraAgent::tick() {
  if (!_ok) return FaceResult::CAMERA_ERROR;

  unsigned long now = millis();
  if (now < _nextDetMs) return FaceResult::NONE;  // not yet time for next sample

  _nextDetMs = now + CAMERA_DETECT_INTERVAL_MS;

  FaceResult current = _runDetection();
  FaceResult edge    = FaceResult::NONE;

  if (current == FaceResult::DETECTED) {
    _lastDetMs = now;
    if (_prevResult != FaceResult::DETECTED) {
      edge = FaceResult::DETECTED;  // fire only on NONE → DETECTED transition
    }
  }

  _prevResult = current;
  _lastResult = current;
  return edge;
}

// ── MJPEG stream (FreeRTOS task on core 0) ───────────────────────────────────
// Runs independently of the Arduino loop() (which runs on core 1).
// One client served at a time; new connections queue in the WiFiServer buffer.

void CameraAgent::_streamTaskFn(void*) {
  for (;;) {
    if (!_streamSrv) { delay(100); continue; }

    WiFiClient client = _streamSrv->accept();
    if (!client) { delay(10); continue; }

    // Send HTTP multipart header once
    client.println("HTTP/1.1 200 OK");
    client.print("Content-Type: ");
    client.println(STREAM_CT);
    client.println("Connection: close");
    client.println();

    while (client.connected()) {
      camera_fb_t* fb = esp_camera_fb_get();
      if (!fb) { delay(30); continue; }

      uint8_t* jpgBuf = nullptr;
      size_t   jpgLen = 0;
      bool     doFree = false;

      if (fb->format == PIXFORMAT_JPEG) {
        jpgBuf = fb->buf;
        jpgLen = fb->len;
      } else {
        // YUV422 → JPEG conversion for streaming
        if (frame2jpg(fb, 80, &jpgBuf, &jpgLen)) {
          doFree = true;
        }
      }

      if (jpgBuf && jpgLen > 0) {
        char hdr[80];
        snprintf(hdr, sizeof(hdr), STREAM_HDR, jpgLen);
        client.print(hdr);
        client.write(jpgBuf, jpgLen);
      }

      if (doFree) free(jpgBuf);
      esp_camera_fb_return(fb);

      delay(100);  // ~10 fps ceiling — reduces CPU pressure on detection and comms
    }

    client.print(STREAM_END);
    client.stop();
  }
}

void CameraAgent::startStreamServer() {
  if (_streamSrv) return;
  _streamSrv = new WiFiServer(81);
  _streamSrv->begin();
  // Pin stream task to core 0; Arduino loop() runs on core 1
  xTaskCreatePinnedToCore(_streamTaskFn, "cam_stream", 8192, nullptr, 1, &_streamTask, 0);
  Serial.println("[Camera] MJPEG stream on port 81 (core 0 task)");
}
