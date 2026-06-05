#pragma once

// mDNS hostnames — devices get IPs via DHCP, peer discovery via .local names
#define MDNS_FACEGUARD     "faceguard" // this device: faceguard.local

// HTTP
#define HTTP_PORT          80

// WiFi / Config Portal
#define WIFI_CONNECT_TIMEOUT_MS  15000UL  // STA connect attempt before fallback to portal
#define PORTAL_TIMEOUT_MS       300000UL  // 5 min: no POST → restart
#define WIFI_LOST_TIMEOUT_MS    300000UL  // 5 min disconnected in loop() → restart to portal

// MQTT
#define MQTT_DEFAULT_PORT         1883
#define MQTT_RECONNECT_MS         5000UL  // retry interval when disconnected
#define MQTT_KEEPALIVE_S            60
#define MQTT_SOCKET_TIMEOUT_S        3    // TCP socket op limit (< 5 s Task WDT default)
#define AGENT2_OFFLINE_TIMEOUT_MS 180000UL // no presence message within 3 min → Agent 2 offline (Agent 2 heartbeat ~60s)

// Dashboard session
#define DASHBOARD_SESSION_TTL_MS 1800000UL  // 30 min inactivity

// Hall-effect door sensor
// Door OPEN when: lowerBound < raw < upperBound (with hysteresis on each edge).
// Door CLOSED when: raw < lowerBound or raw > upperBound (with hysteresis).
// Calibrate with serial 'H' key (press while door is CLOSED and magnet is engaged).
#define HALL_DEFAULT_LOWER         1000   // open-zone lower edge; raw at or below → CLOSED
#define HALL_DEFAULT_UPPER         3000   // open-zone upper edge; raw at or above → CLOSED
#define HALL_HYSTERESIS             150   // dead-zone width on each side of each bound
#define HALL_SAMPLE_INTERVAL_MS     50UL  // analog read rate
#define DOOR_DEBOUNCE_MS           200UL  // require stable state for this long before firing

// Face detection timing
#define FACE_RECENT_MS           10000UL  // face seen within this window → report as active

// Camera Agent
#define CAMERA_DETECT_INTERVAL_MS   500UL   // face detection rate (twice per second)
#define CAMERA_PUB_INTERVAL_MS      200UL   // MQTT camera snapshot rate (5 fps)
#define CAMERA_PUB_MAX_BYTES       (48 * 1024) // max JPEG size accepted for MQTT publish

// Face Recognition
// Enrollment window: if no face detected within this period after scheduleEnroll(),
// the pending enrollment is automatically cancelled.
#define CAMERA_ENROLL_TIMEOUT_MS  10000UL
// Cosine similarity threshold: above = KNOWN (per-template best score).
// Observed: enrolled face ≈ 0.920–0.963, stranger ≈ 0.782–0.825.
#define FACE_SIMILARITY_THRESHOLD  0.90f
// Minimum margin between best-user score and second-best-user score.
// Prevents misidentification when two users have similar feature vectors.
// Only applies when ≥2 users are enrolled; single-user scenarios ignore margin.
#define FACE_MARGIN_MIN            0.03f
// Minimum mean L1 gradient per pixel (HOG-lite texture score).
// Observed: real face ≈ 17–27, non-face false-positive ≈ 5–10, blank wall < 1.
// Threshold at 12 cleanly separates non-face noise from actual face texture.
#define FACE_TEXTURE_MIN_STDDEV   12.0f

// Discord Notifier
#define DISCORD_RATE_LIMIT_MS    30000UL    // min interval between same-event alerts
#define DISCORD_TIMEOUT_MS        5000UL    // connect + read timeout
#define DISCORD_FAIL_COOLDOWN_MS 300000UL   // 5 min after network error

// Session Auth
#define LOGIN_LOCKOUT_MS         60000UL    // lockout duration after max fails
#define LOGIN_MAX_FAILS               5

// Face Vote Window
#define FACE_VOTE_WINDOW_MS          10000UL  // UNKNOWN sliding window: oldest of last FACE_VOTE_UNKNOWN_MIN_HITS hits must be within this age
#define FACE_VOTE_IDLE_MS             5000UL  // no face for this long silently resets voter
#define FACE_VOTE_KNOWN_MIN               3   // KNOWN hits required within one burst window
#define FACE_VOTE_KNOWN_WINDOW_MS     8000UL  // burst window for KNOWN hit accumulation
#define FACE_VOTE_UNKNOWN_MIN_HITS       10   // min UNKNOWN frame hits for UNKNOWN_CONFIRMED

// Buzzer
#define BUZZER_DEFAULT_FREQ_HZ       2000U    // passive buzzer tone frequency (Hz)
#define BUZZER_DURATION_MS          60000UL   // auto-cancel alarm after 60 s (calls _cancelAlarm)
#define BUZZER_TEST_DURATION_MS       500UL   // test beep length
