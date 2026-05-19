#pragma once

// Fixed IP assignments (compile-time constants — not user-configurable)
#define IP_INDOOR          "192.168.0.51"
#define IP_OUTDOOR         "192.168.0.52"
#define IP_GATEWAY         "192.168.0.1"
#define IP_SUBNET          "255.255.255.0"

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

// Discord Notifier
#define DISCORD_RATE_LIMIT_MS    30000UL    // min interval between same-state alerts
#define DISCORD_TIMEOUT_MS        5000UL    // connect + read timeout
#define DISCORD_FAIL_COOLDOWN_MS 300000UL   // 5 min after network error

// Session Auth
#define LOGIN_LOCKOUT_MS         60000UL    // lockout duration after max fails
#define LOGIN_MAX_FAILS               5
