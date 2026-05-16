# Repository Guidelines

## Project Background

This repository implements the DualCam Smart Doorway Agent System. The system uses two independent NMK99 ESP32 camera boards with OV2640 cameras:

- Indoor Agent: camera, mandatory door sensor, LED, buzzer, local home-state logic.
- Outdoor Agent: camera, LED, buzzer, visitor detection, unknown visitor alert.

The goal is to demonstrate bidirectional smart-agent cooperation: detecting entry/exit direction, identifying unknown visitor situations, and maintaining shared doorway state. The core feature is agent cooperation and state-machine behavior, not AI recognition accuracy. Favor a stable demo over complex recognition logic.

## Project Structure & Module Organization

Target structure:

```text
include/
  config.h      # Wi-Fi, fixed IPs, timing constants
  states.h      # SystemState enum, StateEvent struct
  messages.h    # JSON keys and message names
  pins.h        # GPIO assignments
lib/
  AgentProtocol/      # HTTP REST, JSON encode/decode, protocol helpers
  DoorStateMachine/   # centralized state transition logic; emits StateEvent
  ConfigPortal/       # AP mode, config web server, NVS wifi_ssid/wifi_pw only
  SettingsStore/      # NVS dashboard_pw_hash + discord_url; validation + whitelist
  DashboardServer/    # HTTP dashboard/api/settings routes; PROGMEM HTML
  SessionAuth/        # session token, cookie, TTL, CSRF, brute-force throttle
  DiscordNotifier/    # HTTPS webhook send, TLS, rate limit, failure cooldown
src/
  indoor_main.cpp     # indoor-specific setup/loop orchestration; StateEvent → DiscordNotifier
  outdoor_main.cpp    # outdoor-specific setup/loop orchestration; StateEvent → DiscordNotifier
test/                 # PlatformIO tests
```

Keep shared behavior in `include/` and `lib/`. Keep agent-specific wiring, setup, and high-level loop calls in the matching `src/*_main.cpp`.

Each agent owns its own sense-compute-actuate loop and must continue making local decisions if the peer is offline. Peer communication shares state and confirms cooperation; it must not be required for local sensing, processing, or actuation.

## PlatformIO Environments & Commands

This must remain one PlatformIO project with two environments:

```ini
[env:indoor]
platform = espressif32
board = esp32dev
framework = arduino
build_src_filter = +<indoor_main.cpp>

[env:outdoor]
platform = espressif32
board = esp32dev
framework = arduino
build_src_filter = +<outdoor_main.cpp>
```

Do not use `board = esp32cam`. The current `platformio.ini` may still need migration from `esp32dev` to `indoor`/`outdoor`.

Common commands:

- `pio run -e indoor`: build Indoor Agent firmware.
- `pio run -e outdoor`: build Outdoor Agent firmware.
- `pio run -e indoor -t upload`: flash Indoor Agent.
- `pio run -e outdoor -t upload`: flash Outdoor Agent.
- `pio device monitor --port COMX`: open serial logs.
- `pio test -e indoor` / `pio test -e outdoor`: run tests.

## Development Flow

Implement in phases. Do not skip ahead to AI before the cooperation demo is reliable.

1. Hardware basics: LED, buzzer, door sensor, serial logs.
2. Networking — implement sub-steps in order:
   a. Config Portal: first-boot Wi-Fi credential provisioning.
   b. Wi-Fi STA connection (15 s timeout → fallback to portal), fixed-IP, peer REST API.
   c. Dashboard read-only (`/dashboard` + `/api/status`) with Session Auth (login, logout, cookie, TTL, brute-force throttle).
   d. Settings write (`/settings` + CSRF) with SettingsStore; enforce dashboard password change on first login.
   e. Discord Notifier: URL whitelist, CA TLS, rate limit, 5-min failure cooldown.
3. State machine: leaving, entering, occupied/empty, unknown visitor, alert mode; add `StateEvent` emission.
4. Camera: init, stream, then face detect.
5. AI recognition: known/unknown recognition only after earlier phases are stable.

Stability priority is power, door sensor, communication, state machine, camera, then AI.

## Agent Communication Rules

## WiFi Configuration / Config Portal Rules

Both agents use a shared `lib/ConfigPortal/` library for first-boot credential provisioning. Agents call `ConfigPortal::begin(apName)` inside `setup()` before attempting `WiFi.begin()`.

Boot logic:
1. Read `wifi_ssid` and `wifi_pw` from NVS namespace `"agent_cfg"`.
2. If found: call `WiFi.begin()` with a 15-second timeout. On failure, fall into Config Portal.
3. If not found: start Config Portal immediately.
4. If no POST `/save` received within 5 minutes: restart and retry.

Config Portal constraints:
- Serve one lightweight HTML page (SSID + Password form only, under 4 KB). No other config here.
- AP name `"DualCam-Indoor-Setup"` / `"DualCam-Outdoor-Setup"`, password `dualcam99`.
- On POST `/save`: persist `wifi_ssid`/`wifi_pw` to NVS, then `ESP.restart()`.
- No auth required on Config Portal (AP mode only; no internet).

Hard constraints for all agents:
- `ConfigPortal` owns only `wifi_ssid` and `wifi_pw`. All other NVS keys (`dashboard_pw_hash`, `discord_url`) are owned by `SettingsStore`.
- Never store SystemState, sensor readings, or runtime data to NVS.
- Fixed IPs (`192.168.1.51` / `192.168.1.52`) are compile-time constants; never make them configurable.
- No external CDN, React, or Vue anywhere; HTML must fit in ESP32 DRAM.

## Dashboard, Auth, and Settings Rules

After Wi-Fi connects, `DashboardServer` registers routes on the existing WebServer.

Route protection: every route except `/login` and `/logout` must pass `SessionAuth::isAuthorized(server)`. Return HTTP 302 to `/login` on failure.

Session Auth constraints:
- Username `admin` is hardcoded. Password stored as salted SHA-256 hex in `dashboard_pw_hash`.
- First visit to `/dashboard` with an unconfigured or default password must redirect to a password-change form.
- Session token: 16-byte random hex, in-memory, 30-min TTL (reset on each authorized request).
- Cookie: `sid=<token>; HttpOnly; Path=/; SameSite=Lax`. No `Secure` flag (HTTP only; document this limitation in serial log at boot).
- Brute-force: 5 consecutive failed logins → 60-second in-memory lockout.
- Single active session: new login invalidates previous token.

CSRF constraints:
- Every HTML form that causes a state change (settings save, password change) must embed a CSRF token hidden field.
- `POST /settings/save` must validate the CSRF token before any NVS write.

SettingsStore constraints:
- Validate all inputs before writing to NVS: URL must start with `https://discord.com/api/webhooks/` or `https://discordapp.com/api/webhooks/` and be ≤ 256 chars; password 8–64 printable chars.
- Reject empty or whitespace-only values.

Peer status on Dashboard:
- Use a cached last-known peer status value updated periodically in `loop()`. Do NOT make a blocking peer HTTP call inside the `/api/status` route handler.

## Discord Notification Rules

`DiscordNotifier::notify(webhookUrl, message)` is the sole public API. Returns `true` on success, `false` on rate-limit, cooldown, offline, or error.

Agents call `notify()` from the orchestration layer (inside `indoor_main.cpp` / `outdoor_main.cpp`) when a `StateEvent` indicates entry into `UNKNOWN_VISITOR` or `ALERT_MODE`. `DoorStateMachine` must not call `DiscordNotifier` directly.

Mandatory checks before any network I/O (fail fast and return false):
1. WiFi not connected.
2. `webhookUrl` does not match the allowed prefix whitelist.
3. Rate limit active (< 30 s since last successful notify for the same state).
4. Failure cooldown active (< 5 min since last network error).

TLS: use `WiFiClientSecure` with CA root certificate by default. `setInsecure()` is only compiled in when `DISCORD_TLS_INSECURE` is defined; that flag must never be set in production builds.

Network timeout: 5 seconds total (connect + read). On timeout, activate the 5-min failure cooldown.

Log masking: serial logs may print only the last 8 characters of the webhook URL. Never log the full URL, token, or session credentials.

## Agent Communication Rules

Use HTTP REST JSON only in current phases. Do not introduce MQTT yet. Communication must be request/response and bidirectional. Minimum required interaction lines:

1. Indoor requests `outside_status`.
2. Outdoor responds with `outside_status`.
3. Outdoor requests `home_state`.
4. Indoor responds with `home_state`.

Each agent must be able to send requests, receive requests, send responses, and receive responses.

## State Machine Rules

Centralize state logic in `lib/DoorStateMachine/`; do not scatter transitions through `loop()`.

Required states:

```cpp
enum class SystemState {
  IDLE,
  PREPARE_TO_LEAVE,
  PREPARE_TO_ENTER,
  LEAVING_HOME,
  ENTERING_HOME,
  HOME_OCCUPIED,
  HOME_EMPTY,
  UNKNOWN_VISITOR,
  ALERT_MODE
};
```

Required timing constants:

- Indoor face recent window: 10 seconds.
- Outdoor face recent window: 10 seconds.
- Unknown visitor timeout: 15 seconds.
- Door debounce: 100-300 ms.

Timeouts must return the system to `IDLE`.

## Hardware, Camera, and GPIO Rules

Door sensor is mandatory and confirms entering/leaving. Do not rely on camera alone.

Camera development order is camera init, stream, face detect, then known/unknown recognition. Start with `FRAMESIZE_QVGA`.

Avoid GPIO 0, 2, 6-11, and 12. Prefer GPIO 25, 26, 27, 32, 33, 34, and 35.

Use these fixed IPs only:

- Indoor: `192.168.1.51`
- Outdoor: `192.168.1.52`

Do not add dynamic discovery during the current phases.

## Coding Style & Naming

Use C++ with two-space indentation and same-line braces. Use `PascalCase` for types, `camelCase` for functions and variables, and `UPPER_SNAKE_CASE` for constants/macros. Prefer small functions with clear hardware or protocol intent, such as `readDoorSensor()`, `sendOutdoorStatusRequest()`, or `enterAlertMode()`.

## Testing, Logging, and Demo Validation

Place tests under `test/`. Test reusable protocol, serialization, debounce, and state-machine logic when possible. Before submitting firmware changes, build the affected environment and record manual verification.

All important events must log with an agent prefix:

```text
[Indoor] request outside_status
[Outdoor] response outside_status
[Indoor] door opened
[Indoor] state -> LEAVING_HOME
```

Minimum demo scenarios:

- Leaving detection.
- Entering detection.
- Unknown visitor alert.
- Each agent independently demonstrates sensing, local processing, and local actuation.

## Commit, PR, and Security Guidelines

Use short action-style commits, for example `add indoor HTTP server`, `add state machine`, or `add door debounce`. Pull requests should describe affected agent(s), hardware tested, commands run, upload result, and serial-monitor observations.

Never commit `.pio/`, Wi-Fi passwords, tokens, generated binaries, local COM-port settings, or private network secrets.
