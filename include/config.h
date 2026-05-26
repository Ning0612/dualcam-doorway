#pragma once

// mDNS hostnames — devices get IPs via DHCP, peer discovery via .local names
#define MDNS_AGENT1        "agent1"    // this device: agent1.local
#define MDNS_AGENT2        "agent2"    // peer device: agent2.local

// HTTP
#define HTTP_PORT          80

// WiFi / Config Portal
#define WIFI_CONNECT_TIMEOUT_MS  15000UL  // STA connect attempt before fallback to portal
#define PORTAL_TIMEOUT_MS       300000UL  // 5 min: no POST → restart
#define WIFI_LOST_TIMEOUT_MS    300000UL  // 5 min disconnected in loop() → restart to portal

// Peer polling (legacy HTTP fallback; primary comm is MQTT)
#define PEER_QUERY_INTERVAL_MS    5000UL

// MQTT
#define MQTT_DEFAULT_PORT         1883
#define MQTT_RECONNECT_MS         5000UL  // retry interval when disconnected
#define MQTT_KEEPALIVE_S            60
#define AGENT2_OFFLINE_TIMEOUT_MS 15000UL // no presence message within → Agent 2 offline

// Dashboard session
#define DASHBOARD_SESSION_TTL_MS 1800000UL  // 30 min inactivity

// Hall-effect door sensor
#define HALL_DEFAULT_THRESHOLD    2048    // mid-scale starting point; calibrate with 'H' key
#define HALL_HYSTERESIS            150    // dead-zone on each side of threshold (±0–4095 scale)
#define HALL_SAMPLE_INTERVAL_MS     50UL  // analog read rate
#define DOOR_DEBOUNCE_MS           200UL  // require stable state for this long before firing

// Face detection timing
#define FACE_RECENT_MS           10000UL  // face seen within this window → report as active

// Camera Agent
#define CAMERA_DETECT_INTERVAL_MS   500UL   // face detection rate (twice per second)

// Face Recognition
// Enrollment window: if no face detected within this period after scheduleEnroll(),
// the pending enrollment is automatically cancelled.
#define CAMERA_ENROLL_TIMEOUT_MS  10000UL
// Cosine similarity threshold: above = KNOWN (per-template best score).
// Lowered to 0.90 to accommodate multi-template diversity across lighting/angle.
#define FACE_SIMILARITY_THRESHOLD  0.90f
// Minimum margin between best-user score and second-best-user score.
// Prevents misidentification when two users have similar feature vectors.
// Only applies when ≥2 users are enrolled; single-user scenarios ignore margin.
#define FACE_MARGIN_MIN            0.03f
// Minimum mean L1 gradient per pixel (HOG-lite texture score).
// Uniform scenes (ceiling, wall) have near-zero gradient and are rejected.
// Typical values: blank wall ≈ 0.0–0.3, face ≈ 1.5–8.0. Tune via serial log.
#define FACE_TEXTURE_MIN_STDDEV   1.5f

// Discord Notifier
#define DISCORD_RATE_LIMIT_MS    30000UL    // min interval between same-event alerts
#define DISCORD_TIMEOUT_MS        5000UL    // connect + read timeout
#define DISCORD_FAIL_COOLDOWN_MS 300000UL   // 5 min after network error

// Session Auth
#define LOGIN_LOCKOUT_MS         60000UL    // lockout duration after max fails
#define LOGIN_MAX_FAILS               5

// Face Vote Window
#define FACE_VOTE_WINDOW_MS          10000UL  // min elapsed time for UNKNOWN_CONFIRMED
#define FACE_VOTE_IDLE_MS             5000UL  // no face for this long silently resets voter
#define FACE_VOTE_KNOWN_MIN               3   // KNOWN hits required within one burst window
#define FACE_VOTE_KNOWN_WINDOW_MS     8000UL  // burst window for KNOWN hit accumulation
#define FACE_VOTE_UNKNOWN_MIN_HITS       10   // min UNKNOWN frame hits for UNKNOWN_CONFIRMED

// Buzzer
#define BUZZER_DEFAULT_FREQ_HZ       2000U    // passive buzzer tone frequency (Hz)
#define BUZZER_DURATION_MS          60000UL   // auto-silence after 60 s (alarm state unchanged)
#define BUZZER_TEST_DURATION_MS       500UL   // test beep length
