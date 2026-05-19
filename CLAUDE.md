# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

DualCam Smart Doorway Agent System — two NMK99 ESP32 (OV2640) camera agents cooperating over HTTP REST to detect entry/exit direction and unknown visitors. **Agent cooperation is the core feature; camera recognition is secondary.**

---

## Build & Flash

On Windows, `pio` is not in PATH. Use the full path:

```powershell
# Build
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e indoor
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e outdoor

# Upload
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e indoor -t upload
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e outdoor -t upload

# Serial monitor (replace COMX with actual port)
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor --port COMX
```

`platformio.ini` must define two environments (`indoor`, `outdoor`) with `board = esp32dev` and `framework = arduino`. **Never use `board = esp32cam`.**

`build_src_filter` must start with `-<*>` to exclude all sources first, then include only the target file. Libraries in `lib/` do NOT automatically see `include/` — add `-I include` explicitly in every named environment's `build_flags`:

```ini
[env]
platform      = espressif32
board         = esp32dev
framework     = arduino
monitor_speed = 115200

[env:indoor]
build_flags      = -I include -D INDOOR_AGENT
build_src_filter = -<*> +<indoor_main.cpp>

[env:outdoor]
build_flags      = -I include -D OUTDOOR_AGENT
build_src_filter = -<*> +<outdoor_main.cpp>
```

---

## Architecture

### Two Independent Agents

| Agent | Fixed IP | Authority |
|-------|----------|-----------|
| Indoor | `192.168.0.51` | Home state, door sensor, leave/enter detection |
| Outdoor | `192.168.0.52` | Visitor detection, unknown-visitor alert |

Each agent owns its own sense→compute→actuate loop and must function without depending on the peer for local decisions. Communication is HTTP REST JSON only (no MQTT in current phases).

### Minimum Required Interaction Flows

All four must be demonstrated bidirectionally:
1. Indoor → Outdoor: request `outside_status`
2. Outdoor → Indoor: respond `outside_status`
3. Outdoor → Indoor: request `home_state`
4. Indoor → Outdoor: respond `home_state`

### Shared vs. Per-Agent Code

- **`include/`** — `config.h` (IPs, Wi-Fi, timing constants), `states.h` (SystemState enum), `messages.h` (JSON field constants), `pins.h` (GPIO assignments)
- **`lib/AgentProtocol/`** — HTTP client/server helpers, JSON encode/decode for peer REST
- **`lib/DoorStateMachine/`** — centralized state transition logic; emits `StateEvent {from, to, timestamp}` for orchestration layer
- **`lib/ConfigPortal/`** — AP mode, config web server, NVS `wifi_ssid`/`wifi_pw` only; no runtime settings
- **`lib/SettingsStore/`** — NVS read/write for `dashboard_pw_hash` and `discord_url`; enforces key length limits and URL whitelist
- **`lib/DashboardServer/`** — HTTP routes for dashboard, API, settings; PROGMEM HTML; depends on `SessionAuth` and `SettingsStore`
- **`lib/SessionAuth/`** — session token generation, cookie parse/set, TTL (30 min), brute-force throttle (5 attempts → 60 s lockout), CSRF token
- **`lib/DiscordNotifier/`** — HTTPS webhook send, TLS verification, rate limit, failure cooldown
- **`src/indoor_main.cpp`** — door sensor, home state management, respond to outdoor requests; orchestrates StateEvent → DiscordNotifier
- **`src/outdoor_main.cpp`** — face detection, unknown visitor alert, query home state; orchestrates StateEvent → DiscordNotifier

### State Machine

```cpp
enum class SystemState {
  IDLE, PREPARE_TO_LEAVE, PREPARE_TO_ENTER,
  LEAVING_HOME, ENTERING_HOME,
  HOME_OCCUPIED, HOME_EMPTY,
  UNKNOWN_VISITOR, ALERT_MODE
};
```

Required timing (define in `config.h`):
- Face detection recency window: **10 s** (both agents)
- Unknown visitor timeout: **15 s** → return to IDLE
- Door debounce: **100–300 ms**

### Camera

Init order: init → stream → face detect → known/unknown recognition. Start with `FRAMESIZE_QVGA`. Do not implement recognition until earlier stages are stable.

---

## GPIO Constraints

**Avoid:** GPIO 0, 2, 6–11, 12  
**Prefer:** GPIO 25, 26, 27, 32, 33, 34, 35

**GPIO 34, 35, 36, 39 are input-only and have NO internal pull-up resistor.** `INPUT_PULLUP` is silently ignored on these pins. If used for a door sensor or any active-LOW signal, an external 10 kΩ pull-up to 3.3 V is required; use `INPUT` not `INPUT_PULLUP`.

**GPIO 32** is `PWDN` on most ESP32-CAM boards. Safe for Phase 1 (no camera), but must be reassigned when Phase 4 (camera init) begins.

Door sensor is mandatory — do not rely on camera alone to confirm entry/exit.

---

## WiFi Config Portal

Both agents require first-boot credential provisioning via an in-device web portal.

### Boot Flow

```
Power on
  ↓
Read NVS (Preferences) → wifi_ssid / wifi_pw found?
  ├─ Yes → WiFi.begin(ssid, pw) — 15 s timeout
  │         ├─ Connected → normal operation
  │         └─ Failed → enter Config Portal
  └─ No  → enter Config Portal
              ↓
           AP Mode: "DualCam-Indoor-Setup" / "DualCam-Outdoor-Setup"
              ↓
           Serve single-page HTML form (SSID + Password only)
              ↓
           POST /save → write wifi_ssid / wifi_pw to NVS → ESP.restart()
              ↓
           No POST within 5 min → restart and retry
```

### Design Rules

- **Scope**: `ConfigPortal` manages only `wifi_ssid` and `wifi_pw`. All other settings (`dashboard_pw_hash`, `discord_url`) are handled by `SettingsStore`.
- **Fixed IPs are compile-time constants** in `config.h`; they are NOT user-configurable through the portal.
- **HTML budget**: config page under ~4 KB total (inline CSS + JS). No external CDN, no React/Vue.
- **No auth on Config Portal**: it runs only while in AP mode (no internet access), so LAN security is not a concern.
- **AP password**: `dualcam99`.
- **NVS namespace**: `"agent_cfg"` for both agents.
- **API**: `ConfigPortal::begin(apName)` called in `setup()` before `WiFi.begin()`.

### Not in Scope (Config Portal)

- Dynamic IP configuration (IPs remain hardcoded)
- OTA updates
- Multi-network SSID scanning UI
- Dashboard password or Discord URL provisioning

---

## NVS Key Map

All keys share namespace `"agent_cfg"`. Each key is owned by exactly one library.

| Key | Owner Library | Type | Notes |
|-----|--------------|------|-------|
| `wifi_ssid` | `ConfigPortal` | String | max 32 chars |
| `wifi_pw` | `ConfigPortal` | String | max 64 chars |
| `dashboard_pw_hash` | `SettingsStore` | String | salted SHA-256 hex; default forces change |
| `discord_url` | `SettingsStore` | String | must match `https://discord.com/api/webhooks/` prefix; max 256 chars |
| `face_feat` | `FaceRecognizer` | Blob | MAX_FACES × 32 × 4 bytes (float32 L2-normalized block-luminance vectors) |
| `face_cnt` | `FaceRecognizer` | UInt8 | enrolled face count; 0 = none |

---

## Dashboard & Web UI

The existing WebServer (port 80) is extended with dashboard routes after Wi-Fi connects.

### Routes

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| GET | `/` | — | Redirect to `/dashboard` or `/login` |
| GET | `/login` | — | Login form (PROGMEM HTML) |
| POST | `/login` | — | Verify credentials; set `sid` cookie; redirect |
| GET | `/logout` | session | Invalidate token; redirect to `/login` |
| GET | `/dashboard` | session | Status page; polls `/api/status` every 3 s via AJAX |
| GET | `/api/status` | session | JSON: `{state, door, face, peer_status, uptime}` |
| GET | `/settings` | session | Settings form with CSRF token |
| POST | `/settings/save` | session + CSRF | Validate inputs; write via `SettingsStore`; redirect |

### Design Rules

- **HTML storage**: PROGMEM `const char[]`. No LittleFS. Total per page < 6 KB (inline CSS + JS).
- **Status polling**: `setInterval(() => fetch('/api/status'), 3000)` — no SSE, no WebSocket.
- **JSON budget**: `StaticJsonDocument<512>` for `/api/status`. No dynamic `String` concatenation in route handlers.
- **Peer status**: query peer agent's `/api/status`; on timeout show `"offline"`. Do NOT block the route; use a cached last-known value updated in `loop()`.
- **No external CDN, no React/Vue** in any page.

---

## Session Auth

### Rules

- Username: `admin` (hardcoded).
- Password: stored as salted SHA-256 hex in NVS key `dashboard_pw_hash`. Default password forces change flow on first dashboard visit.
- Session token: 16-byte random hex generated on login; stored in-memory; invalidated on `ESP.restart()` or `logout`.
- Cookie: `sid=<token>; HttpOnly; Path=/; SameSite=Lax`.
- TTL: 30 min of inactivity; reset on each authorized request.
- Single active session: new login invalidates previous token.
- CSRF: per-session CSRF token included in all HTML forms; validated on every state-changing POST.
- Brute-force: 5 failed logins → 60 s lockout (in-memory counter; reset on restart).

### Security Caveats (document in serial log at boot)

- No HTTPS — cookie can be sniffed on LAN. This system is NOT safe for internet exposure.
- Session token resets on restart, requiring re-login.

---

## Discord Webhook Notifications

### Library: `lib/DiscordNotifier/`

API:
```cpp
// Returns true if sent, false if rate-limited, offline, or error.
bool DiscordNotifier::notify(const String& webhookUrl, const String& message);
```

### Design Rules

- **TLS**: use `WiFiClientSecure` with CA root certificate by default. `setInsecure()` is available only when `DISCORD_TLS_INSECURE` compile flag is set; it is OFF by default.
- **URL whitelist**: reject any URL that does not start with `https://discord.com/api/webhooks/` or `https://discordapp.com/api/webhooks/`. Return false immediately.
- **Timeout**: 5 s total (connect + read). On timeout, enter 5-min failure cooldown.
- **Rate limit**: state-entry event fires notify once. Minimum 30 s between any two notifications of the same `SystemState`. If rate limit active, return false without network call.
- **Non-blocking**: all checks (WiFi connected, rate limit, cooldown) happen before any network I/O. If any check fails, return false immediately.
- **Event source**: `DoorStateMachine` emits `StateEvent {from, to, timestamp}`. Agent orchestration layer (`indoor_main.cpp` / `outdoor_main.cpp`) receives the event and calls `DiscordNotifier::notify()`. The state machine does NOT call the notifier directly.
- **Triggers**: notify on entry into `UNKNOWN_VISITOR` and `ALERT_MODE`.
- **Log masking**: print only the last 8 characters of the webhook URL; never log the full URL or token.

---

## Development Phases

Implement in order:
1. Hardware basics (LED, buzzer, door sensor, serial logs)
2. Networking — implement in this sub-order:
   a. Config Portal: first-boot Wi-Fi credential provisioning
   b. Wi-Fi STA connection (15 s timeout → fallback to portal), fixed-IP assignment, peer REST API
   c. Dashboard read-only (`/dashboard` + `/api/status`) with Session Auth (login / logout / cookie / TTL / brute-force)
   d. Settings write (`/settings` + CSRF) with `SettingsStore`; dashboard password change flow
   e. Discord Notifier: URL whitelist + CA TLS + rate limit + failure cooldown
3. State machine (leaving/entering/alert transitions; add `StateEvent` emission)
4. Camera (init, stream, face detect)
5. AI (known/unknown recognition)

Stability priority: power → door sensor → communication → state machine → camera → AI.

---

## Logging Convention

```
[Indoor] face detected
[Indoor] request outside_status
[Outdoor] response outside_status
[Indoor] door opened
[Indoor] state -> LEAVING_HOME
```
