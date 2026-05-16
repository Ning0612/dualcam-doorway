#pragma once

// Fixed IP assignments (compile-time constants — not user-configurable)
#define IP_INDOOR          "192.168.1.51"
#define IP_OUTDOOR         "192.168.1.52"
#define IP_GATEWAY         "192.168.1.1"
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

// Peer polling
#define PEER_QUERY_INTERVAL_MS    5000UL

// Dashboard session
#define DASHBOARD_SESSION_TTL_MS 1800000UL  // 30 min inactivity

// Discord Notifier
#define DISCORD_RATE_LIMIT_MS    30000UL    // min interval between same-state alerts
#define DISCORD_TIMEOUT_MS        5000UL    // connect + read timeout
#define DISCORD_FAIL_COOLDOWN_MS 300000UL   // 5 min after network error

// Session Auth
#define LOGIN_LOCKOUT_MS         60000UL    // lockout duration after max fails
#define LOGIN_MAX_FAILS               5
