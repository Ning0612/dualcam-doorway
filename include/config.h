#pragma once

// mDNS hostnames — devices get IPs via DHCP, peer discovery via .local names
#define MDNS_INDOOR        "indoor-agent"   // reachable as indoor-agent.local
#define MDNS_OUTDOOR       "outdoor-agent"  // reachable as outdoor-agent.local

// HTTP
#define HTTP_PORT          80

// State machine timing (milliseconds)
#define FACE_RECENT_MS         10000UL
#define UNKNOWN_VISITOR_MS     15000UL
#define DOOR_DEBOUNCE_MS         200UL
#define DOOR_TRANSITION_MS     30000UL

// WiFi / Config Portal
#define WIFI_CONNECT_TIMEOUT_MS  15000UL  // STA connect attempt before fallback to portal
#define PORTAL_TIMEOUT_MS       300000UL  // 5 min: no POST → restart
#define WIFI_LOST_TIMEOUT_MS    300000UL  // 5 min disconnected in loop() → restart to portal

// Serial 'W' command clears WiFi credentials. A hardware BOOT button is NOT used because
// GPIO 0 doubles as CAM_XCLK on the NMK99 — driving it LOW would corrupt the camera clock.

// Peer polling
#define PEER_QUERY_INTERVAL_MS    5000UL

// Dashboard session
#define DASHBOARD_SESSION_TTL_MS 1800000UL  // 30 min inactivity

// Hall-effect door sensor (indoor only)
#define HALL_DEFAULT_THRESHOLD    2048    // mid-scale starting point; calibrate with 'H' key
#define HALL_HYSTERESIS            150    // dead-zone on each side of threshold (±0-4095 scale)
#define HALL_SAMPLE_INTERVAL_MS     50UL  // analog read rate

// Camera Agent
#define CAMERA_DETECT_INTERVAL_MS   500UL   // face detection rate (twice per second)

// Face Recognition (Phase 5)
// Enrollment window: if no face is detected within this period after scheduleEnroll(),
// the pending enrollment is automatically cancelled.
#define CAMERA_ENROLL_TIMEOUT_MS  10000UL
// Cosine similarity threshold: above = KNOWN. Features are L2-normalized
// block-luminance vectors; re-enroll if lighting conditions change significantly.
#define FACE_SIMILARITY_THRESHOLD  0.92f

// Discord Notifier
#define DISCORD_RATE_LIMIT_MS    30000UL    // min interval between same-state alerts
#define DISCORD_TIMEOUT_MS        5000UL    // connect + read timeout
#define DISCORD_FAIL_COOLDOWN_MS 300000UL   // 5 min after network error

// Session Auth
#define LOGIN_LOCKOUT_MS         60000UL    // lockout duration after max fails
#define LOGIN_MAX_FAILS               5

// Face Vote Window (outdoor agent)
#define FACE_VOTE_WINDOW_MS   30000UL  // 30s of sustained UNKNOWN with no KNOWN hit triggers alert
#define FACE_VOTE_IDLE_MS      5000UL  // no face for this long resets the voter window silently
#define FACE_VOTE_KNOWN_MIN       1    // min KNOWN hits in window to confirm (raise to 2 for stricter)
