#pragma once

// Wi-Fi — replace with actual credentials, do NOT commit real values
#define WIFI_SSID          "YOUR_SSID"
#define WIFI_PASSWORD      "YOUR_PASSWORD"

// Fixed IP assignments
#define IP_INDOOR          "192.168.1.51"
#define IP_OUTDOOR         "192.168.1.52"
#define IP_GATEWAY         "192.168.1.1"
#define IP_SUBNET          "255.255.255.0"

// HTTP
#define HTTP_PORT          80

// Timing constants (milliseconds)
#define FACE_RECENT_MS      10000UL  // face detection recency window
#define UNKNOWN_VISITOR_MS  15000UL  // unknown visitor timeout → IDLE
#define DOOR_DEBOUNCE_MS    200UL    // door sensor debounce
#define DOOR_TRANSITION_MS  30000UL  // LEAVING/ENTERING stuck guard — returns to IDLE if door event lost
