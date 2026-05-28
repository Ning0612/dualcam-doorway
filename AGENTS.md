# Repository Guidelines

## Project Overview

This repository implements **FaceGuard: ESP32 Smart Doorway Security System** — a single-board embedded system on ESP32 NMK99 + OV2640 camera. The primary goal is a standalone doorway security agent with optional coordination with a separate Agent 2 (indoor presence agent) via MQTT.

> **Single environment**: this is a single-agent PlatformIO project. There is only one build environment (`faceguard`). There are no `indoor`/`outdoor` environments.

---

## PlatformIO Build Commands

`pio` is not in PATH on Windows. Use the full path:

```powershell
# Build
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e faceguard

# Flash (replace COM3 with actual port)
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e faceguard -t upload --upload-port COM3

# Serial monitor
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor --port COM3
```

Environment config (`platformio.ini`):

```ini
[env:faceguard]
; DISCORD_TLS_INSECURE: dev/test only — replace with DISCORD_ROOT_CA_CERT for production
build_unflags    = -O2 -O3 -Og
build_flags      = -I include -D FACEGUARD -D DISCORD_TLS_INSECURE
                   -D BOARD_HAS_PSRAM -mfix-esp32-psram-cache-issue -Os
board_build.partitions = huge_app.csv
build_src_filter = -<*> +<main.cpp>
```

**Never use `board = esp32cam`.** Libraries in `lib/` do NOT automatically see `include/` — `-I include` is required.

---

## Project Structure

```
src/
  main.cpp              # All callbacks, setup(), loop()
lib/
  FaceRecognizer/       # HOG-lite 64-dim feature, NVS face storage
  FaceVoter/            # Temporal voting (prevents single-frame trigger)
  CameraAgent/          # Camera init, MJPEG stream, enroll scheduling
  DoorSensor/           # Hall sensor ADC, dual-bound hysteresis
  LedController/        # WS2812B single NeoPixel
  BuzzerController/     # Passive buzzer, LEDC PWM
  SecurityStateMachine/ # AlertLevel logic, alarm callbacks
  AgentComm/            # MQTT publish/subscribe
  DiscordNotifier/      # HTTPS webhook, TLS, rate limiting
  ConfigPortal/         # AP mode first-boot WiFi provisioning
  ConfigManager/        # MQTT + buzzer NVS settings
  SettingsStore/        # Password, Discord URL, hall bounds NVS
  SessionAuth/          # Session token, CSRF, brute-force throttle
  DashboardServer/      # HTTP routes, PROGMEM HTML, AJAX API
  LogManager/           # RAM ring buffer + SPIFFS NDJSON logs
include/
  config.h              # All timing constants
  pins.h                # GPIO assignments (LED=32, BUZZER=13, HALL=33)
  states.h              # DoorState, FaceState, AlertLevel, VoteResult, AlertEvent enums
  messages.h            # MQTT topics and JSON field constants
```

---

## Core Architecture Rules

### Face Recognition
- **Algorithm**: HOG-lite — 4×4 grid cells × 4 orientation bins = **64-dim** feature vector
- **Gradient**: central difference (`dx = Y(x+1) - Y(x-1)`), NOT Sobel
- **Input**: YUV422 Y-channel only, QVGA 320×240, central 60% ROI
- **Pipeline order**: extract → per-cell L1 norm → active-cell gate (< 4 active → NO_FACE) → global L2 norm → texture check → similarity
- **Similarity threshold**: `FACE_SIMILARITY_THRESHOLD` = 0.90; margin = 0.03
- Single-frame results are never used directly — must pass through `FaceVoter`

### FaceVoter
- KNOWN_CONFIRMED: ≥ 3 hits within 8 s window
- UNKNOWN_CONFIRMED: persistent UNKNOWN for ≥ 10 s with ≥ 10 hits
- Idle reset: 5 s with no face detected

### SecurityStateMachine
- **Pure callback architecture**: SSM never directly controls hardware or sends network requests
- All hardware/network side effects go through registered callbacks (`setOnAlert`, `setOnDoorEvent`, `setOnKnownConfirmed`, `setOnAlarmCancelled`, `setOnBuzzerSilence`)
- `_cancelAlarm()` fires both `_onBuzzerSilence` and `_onAlarmCancelled` callbacks
- The alarm is always cancelled when: buzzer duration expires, door closes, or KNOWN_CONFIRMED received

### LED Priority (updateLed() in main.cpp)
1. `isAlarmActive()` → red blink (250 ms)
2. `ALERT_GREEN` → solid green
3. Face detected (any type) → solid white
4. Otherwise → off

### MQTT
- Publish: `home/security/door`, `home/security/face`, `home/security/alert`, `home/security/status`
- Subscribe: `home/home_state/presence`, `home/home_state/alarm_decision`
- Agent 2 offline (no presence for 15 s) → ALERT_RED, independent operation

---

## WebUI Routes

Full route list in `CLAUDE.md` Section 11 and `docs/webui.md`. Key rules:
- All state-changing POSTs require CSRF token
- `/settings` is the unified settings page (Discord, MQTT, Hall bounds, Buzzer, Password)
- No WiFi settings in WebUI — WiFi only via Config Portal (AP mode)
- HTML stored in PROGMEM `const char[]`; no LittleFS, no external CDN

---

## NVS Keys (namespace `"agent_cfg"`)

| Key | Owner | Notes |
|-----|-------|-------|
| `wifi_ssid`, `wifi_pw` | ConfigPortal | String |
| `dashboard_pw` | SettingsStore | salted SHA-256 hex |
| `pw_changed` | SettingsStore | Bool |
| `discord_url` | SettingsStore | max 256 chars |
| `hall_lo`, `hall_hi` | SettingsStore | UInt32; form fields use `hall_lower`/`hall_upper` |
| `mqtt_broker` | ConfigManager | max 63 chars |
| `mqtt_port` | ConfigManager | UInt16 |
| `buzzer_freq`, `buzzer_dur` | ConfigManager | UInt32; WebUI inputs in Hz / seconds |
| `face_feat`, `face_cnt`, `face_names`, `face_tcnt` | FaceRecognizer | Blob |

---

## GPIO Constraints

**Safe:** GPIO 13, 32, 33  
**Prohibited for general use:** 0, 2, 5, 6–11, 12, 18–19, 21–23, 25–27, 34–36, 39

GPIO 32 (NMK99): `CAM_PWDN` unconnected → available for WS2812B. Set `pin_pwdn = -1` in camera config.

---

## Mandatory Constraints

- Never trigger events from a single camera frame — always go through FaceVoter
- Never skip texture validation or active-cell gate in FaceRecognizer
- Never let WebUI routes make security decisions
- Never halt local operation due to WiFi / Discord / MQTT / NTP failure
- Never use large CNN or cloud AI on-device

---

## Commit and Security Guidelines

- Log format: `[FaceGuard] <event description>`
- Never commit `.pio/`, Wi-Fi passwords, tokens, or binaries
- Webhook URL: log only the last 8 characters (`discord sent (last 8: xxxxxxxx)`)
- Do not log full session tokens or passwords
