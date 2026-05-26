# CLAUDE.md

本專案為 **Agent 1：ESP32 智慧警戒保全與門禁紀錄系統**。  
目標是在 ESP32 NMK99 + OV2640 Camera 上實作可獨立運作的門口警戒 agent，並能與 Agent 2（室內狀態 agent）交換狀態。

---

## Build & Flash

On Windows, `pio` is not in PATH. Use the full path:

```powershell
# Build
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e agent1

# Upload
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e agent1 -t upload

# Serial monitor (replace COMX with actual port)
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor --port COMX
```

`platformio.ini` defines a single environment (`agent1`):

```ini
[env]
platform      = espressif32
board         = esp32dev
framework     = arduino
monitor_speed = 115200

[env:agent1]
build_flags      = -I include -D AGENT1
build_src_filter = -<*> +<main.cpp>
```

**Never use `board = esp32cam`.**  
Libraries in `lib/` do NOT automatically see `include/` — the `-I include` flag is required.

---

## 1. 核心目標

Agent 1 必須完成：

- Camera 人臉辨識
- 霍爾感應器偵測門開關
- RGB LED 顯示警戒狀態
- 蜂鳴器警示
- Discord Webhook 通知
- Local WebUI 設定與紀錄查詢
- 與 Agent 2 交換室內狀態與警報決定

**Agent 1 必須能在 Agent 2 離線時獨立運作。**

---

## 2. 硬體

### Sensors
- OV2640 Camera（人臉辨識）
- 霍爾感應器（偵測門開關）

### Computing
- ESP32 NMK99

### Actuators
- RGB LED（三色：紅/黃/綠）
- 蜂鳴器
- Discord Webhook
- Local WebUI

---

## 3. GPIO 配置

**Avoid:** GPIO 0, 2, 6–11, 12  
**Prefer:** GPIO 25, 26, 27, 32, 33, 34, 35

**GPIO 34, 35, 36, 39 是 input-only，無內部上拉電阻。** 若用於霍爾感應器等 active-LOW 訊號，須外接 10 kΩ 上拉電阻至 3.3 V，並使用 `INPUT`（非 `INPUT_PULLUP`）。

**GPIO 32** 在多數 ESP32-CAM 板上為 `PWDN`，啟用 camera 後不可作為一般 GPIO。

```
PIN_LED_R   = 25   // RGB LED 紅色
PIN_LED_G   = 26   // RGB LED 綠色
PIN_LED_B   = 27   // RGB LED 藍色
PIN_BUZZER  = 13   // 蜂鳴器
PIN_HALL    = 33   // 霍爾感應器（ADC1_CH5，ADC1 支援 WiFi 同時使用）
```

GPIO 0 為 CAM_XCLK，不可作為一般按鈕。WiFi 重設透過 Serial 'W' 指令執行。

---

## 4. 主要狀態定義

```cpp
enum class DoorState {
    DOOR_CLOSED,
    DOOR_OPEN
};

enum class FaceState {
    FACE_NO_FACE,
    FACE_KNOWN,
    FACE_UNKNOWN
};

enum class VoteResult {
    NONE,
    KNOWN_CONFIRMED,
    UNKNOWN_CONFIRMED
};

enum class AlertLevel {
    ALERT_GREEN,   // 已知使用者 / 正常
    ALERT_YELLOW,  // Agent 2 回報室內有人
    ALERT_RED      // Agent 2 離線 或 室內無人（預設）
};

enum class AlarmDecision {
    NO_ACTION,
    TRIGGER_ALARM,
    CANCEL_ALARM
};
```

---

## 5. 人臉辨識規則（FaceRecognizer）

使用 lightweight feature-based face recognition，不使用大型 CNN。

**流程：**

```
Camera frame
  ↓
擷取中央 60% ROI
  ↓
切成 4x4 blocks (16 blocks)
  ↓
每個 block 計算 mean luminance 與 standard deviation → 32 維 feature vector
  ↓
L2 normalization
  ↓
Texture validation（mean stddev < FACE_TEXTURE_MIN_STDDEV → 回傳 FACE_NO_FACE）
  ↓
Cosine similarity matching
  ↓
輸出 FACE_NO_FACE / FACE_KNOWN / FACE_UNKNOWN
```

**規則：**
- 只處理 YUV422 的 Y channel
- ROI 只取中央 60%
- Feature vector 為 32 維（4×4 blocks × 2 值/block）
- 註冊人臉儲存在 NVS（key: `face_feat`, `face_cnt`）
- 最多支援 7 位使用者（`MAX_FACES = 7`）
- Similarity ≥ `FACE_SIMILARITY_THRESHOLD` (0.92) → FACE_KNOWN
- 否則 → FACE_UNKNOWN

---

## 6. FaceVoter 規則

不得使用單一 frame 直接觸發事件，所有辨識結果必須先經過 FaceVoter。

**Known Confirmed：**
- `FACE_VOTE_KNOWN_MIN` (3) hits 在 `FACE_VOTE_KNOWN_WINDOW_MS` (8 s) 內 → `KNOWN_CONFIRMED`
- Confirmed 後記錄 `confirmedName`（從 `FaceRecognizer::getLastMatchName()` 取得）
- 同一張臉連續在畫面中不得重複觸發，直到 idle reset

**Unknown Confirmed：**
- 持續偵測到 UNKNOWN 超過 `FACE_VOTE_WINDOW_MS` (10 s) 且至少 `FACE_VOTE_UNKNOWN_MIN_HITS` (10) 次 → `UNKNOWN_CONFIRMED`
- 偶發 KNOWN raw result 不得立刻重置 unknown timer
- Unknown 持續存在時，每隔 `FACE_VOTE_WINDOW_MS` 可重新觸發

**Idle Reset（`FACE_VOTE_IDLE_MS` = 5 s 無臉）：**
- 清除 counters、timers
- 清除 `confirmedName`
- 回到初始狀態

---

## 7. 警戒狀態機（SecurityStateMachine）

根據 `DoorState`、`VoteResult`、Agent 2 的 `presence_state` 決定 `AlertLevel`。

### AlertLevel 計算

| 條件 | AlertLevel |
|------|------------|
| Agent 2 離線 OR presence = UNOCCUPIED | ALERT_RED |
| presence = OCCUPIED | ALERT_YELLOW |
| KNOWN_CONFIRMED（無告警） | ALERT_GREEN |

### Red Alert 行為

- RGB LED 亮紅色
- 若收到 `UNKNOWN_CONFIRMED`：
  - LED 紅色閃爍
  - 蜂鳴器警示
  - 發送 Discord webhook
  - 記錄警戒事件至 Alert Log

### Yellow Alert 行為

- RGB LED 亮黃色
- 若收到 `UNKNOWN_CONFIRMED`：
  - 通知 Agent 2（MQTT publish）
  - 等待 Agent 2 回傳 `alarm_decision`
  - `TRIGGER_ALARM` → 啟動蜂鳴器 + LED 紅色閃爍
  - `CANCEL_ALARM` → 取消警戒並記錄

### Green / Normal 行為

- RGB LED 亮綠色
- KNOWN_CONFIRMED 時記錄門禁事件

---

## 8. 門禁事件規則

### Known User Confirmed

FaceVoter 輸出 `KNOWN_CONFIRMED` 時：
- 記錄 Face Log（使用者名稱、時間、similarity）
- 更新 WebUI 顯示
- 發送 face event 給 Agent 2（MQTT publish `home/security/face`）

### User Returned（已知使用者 + 門被打開）

- 記錄 Door Log（使用者名稱、時間）
- 發送 Discord：`Agent 1：<name> 回家`
- 發送 known user entry event 給 Agent 2

### Unknown Visitor

FaceVoter 輸出 `UNKNOWN_CONFIRMED` 時：
- 記錄 Alert Log
- 依 AlertLevel 處理警報
- 發送 alert event 給 Agent 2（MQTT publish `home/security/alert`）

---

## 9. WiFi 與 AP 模式

**開機流程：**

```
Power on
  ↓
讀取 NVS → wifi_ssid / wifi_pw 是否存在？
  ├─ Yes → WiFi.begin(ssid, pw) — 15 s timeout
  │         ├─ Connected → NTP sync → 啟動 WebUI → Discord online 通知
  │         └─ Failed → enter Config Portal
  └─ No  → enter Config Portal
              ↓
           AP Mode: "Agent1-Setup"（密碼: dualcam99）
              ↓
           Serve HTML form at 192.168.4.1
              ↓
           POST /save → 寫入 NVS → ESP.restart()
              ↓
           無 POST 5 min → restart
```

**AP / WebUI 至少支援設定：**
- WiFi SSID / Password（含 WiFi Scan）
- Discord Webhook URL
- WebUI 登入帳密
- Face similarity threshold
- FaceVoter 參數

---

## 10. Agent 2 通訊（MQTT）

使用 MQTT，Broker 位址由 ConfigManager 設定。

**Agent 1 Publish：**

```
home/security/door    — DoorState 變更
home/security/face    — KNOWN_CONFIRMED 事件
home/security/alert   — UNKNOWN_CONFIRMED 事件
home/security/status  — 週期性心跳（AlertLevel, uptime 等）
```

**Agent 1 Subscribe：**

```
home/home_state/presence       — Agent 2 室內人員狀態
home/home_state/alarm_decision — Agent 2 警報決定
```

**接收 presence 範例：**

```json
{
  "presence_state": "OCCUPIED",
  "presence_score": 3,
  "timestamp": "2025-12-20T18:30:05+08:00"
}
```

- `OCCUPIED` → `ALERT_YELLOW`
- `UNOCCUPIED` → `ALERT_RED`

**接收 alarm_decision 範例：**

```json
{
  "alarm_decision": "CANCEL_ALARM",
  "source": "button",
  "timestamp": "2025-12-20T18:30:08+08:00"
}
```

- `TRIGGER_ALARM` → 啟動蜂鳴器 + LED 閃爍
- `CANCEL_ALARM` → 取消警戒並記錄
- `NO_ACTION` → 不變更

**Agent 2 離線時（MQTT 斷線或 subscribe 超時）：** 預設維持 `ALERT_RED`，Agent 1 獨立警戒。

---

## 11. Local WebUI

WebUI 為管理介面，不可成為核心決策邏輯。

**必要頁面：**

| 路徑 | Auth | 說明 |
|------|------|------|
| `/` | — | 導向 `/dashboard` 或 `/login` |
| `/login` | — | 登入表單 |
| `/logout` | session | 登出 |
| `/dashboard` | session | 即時狀態（3 s AJAX 輪詢） |
| `/settings/wifi` | session + CSRF | WiFi 設定 |
| `/settings/discord` | session + CSRF | Discord 設定 |
| `/settings/system` | session + CSRF | 其他系統設定 |
| `/face/register` | session | 人臉註冊（含 camera preview） |
| `/face/preview` | session | Camera MJPEG 預覽（port 81） |
| `/log/door` | session | 門禁紀錄 |
| `/log/face` | session | 人臉辨識紀錄 |
| `/log/alert` | session | 警戒事件紀錄 |
| `/api/status` | session | JSON 即時狀態 |
| `/api/face/enroll` | session | 人臉註冊 API |
| `/api/face/clear` | session | 清除人臉資料 API |

**Dashboard 至少顯示：**
- WiFi 狀態、IP address
- 時間（NTP 同步狀態）
- Door state（OPEN/CLOSED）
- Face state（NO_FACE/KNOWN/UNKNOWN）
- Alert level（GREEN/YELLOW/RED）
- Agent 2 連線狀態與 presence state
- 最近門禁 / 警戒事件

**設計限制：**
- HTML 儲存於 PROGMEM `const char[]`，無 LittleFS
- 每頁總大小 < 6 KB（含 inline CSS + JS）
- 無外部 CDN，無 React/Vue
- `/api/status` JSON budget：`StaticJsonDocument<512>`

---

## 12. Session Auth

- Username: `admin`（hardcoded）
- Password: salted SHA-256 hex 存於 NVS key `dashboard_pw_hash`；預設密碼強制變更
- Session token: 16-byte random hex；in-memory；`ESP.restart()` 或 logout 時失效
- Cookie: `sid=<token>; HttpOnly; Path=/; SameSite=Lax`
- TTL: 30 min 無操作；每次授權請求重置
- CSRF: 所有 state-changing POST 驗證 CSRF token
- Brute-force: 5 次失敗 → 60 s lockout

---

## 13. NVS Key Map

所有 key 共用 namespace `"agent_cfg"`。

| Key | Owner | Type | Notes |
|-----|-------|------|-------|
| `wifi_ssid` | ConfigPortal | String | max 32 chars |
| `wifi_pw` | ConfigPortal | String | max 64 chars |
| `dashboard_pw_hash` | SettingsStore | String | salted SHA-256 hex |
| `discord_url` | SettingsStore | String | `https://discord.com/api/webhooks/` prefix; max 256 chars |
| `mqtt_broker` | ConfigManager | String | MQTT broker IP/hostname; max 64 chars |
| `mqtt_port` | ConfigManager | UInt16 | default 1883 |
| `face_feat` | FaceRecognizer | Blob | MAX_FACES × 32 × 4 bytes (float32) |
| `face_cnt` | FaceRecognizer | UInt8 | enrolled face count; 0 = none |

---

## 14. Discord Webhook 通知

```cpp
// Returns true if sent, false if rate-limited, offline, or error.
bool DiscordNotifier::notify(const String& webhookUrl, SystemState state, const String& message);
bool DiscordNotifier::notifyBoot(const String& webhookUrl, const String& message);
```

**觸發事件：**
- `UNKNOWN_CONFIRMED`（Red Alert）
- Known user 回家
- 開機 IP 公告

**設計規則：**
- TLS: `WiFiClientSecure` with CA root cert（`DISCORD_TLS_INSECURE` flag 才能 setInsecure）
- URL whitelist: `https://discord.com/api/webhooks/` 或 `https://discordapp.com/api/webhooks/`
- Timeout: 5 s（connect + read）
- Rate limit: 同 state 最少間隔 30 s
- Non-blocking: 所有前置檢查在 network I/O 前完成
- Log masking: 只印出 webhook URL 最後 8 字元

---

## 15. Log 系統（LogManager）

至少保留最近 50 筆（ESP32 heap 限制，不使用 LittleFS）。

### Face Log

```json
{
  "timestamp": "...",
  "face_state": "FACE_KNOWN",
  "vote_result": "KNOWN_CONFIRMED",
  "user_name": "Alice",
  "similarity": 0.95
}
```

### Door Log

```json
{
  "timestamp": "...",
  "door_state": "DOOR_OPEN",
  "related_user": "Alice"
}
```

### Alert Log

```json
{
  "timestamp": "...",
  "alert_level": "ALERT_RED",
  "alert_type": "UNKNOWN_VISITOR",
  "alarm_decision": "TRIGGER_ALARM",
  "discord_result": true
}
```

---

## 16. 程式架構

```
src/
  main.cpp

lib/
  FaceRecognizer/
    FaceRecognizer.h
    FaceRecognizer.cpp
  FaceVoter/
    FaceVoter.h
    FaceVoter.cpp
  CameraAgent/
    CameraAgent.h
    CameraAgent.cpp
  DoorSensor/
    DoorSensor.h
    DoorSensor.cpp
  LedController/
    LedController.h
    LedController.cpp
  BuzzerController/
    BuzzerController.h
    BuzzerController.cpp
  SecurityStateMachine/
    SecurityStateMachine.h
    SecurityStateMachine.cpp
  AgentComm/
    AgentComm.h
    AgentComm.cpp
  DiscordNotifier/
    DiscordNotifier.h
    DiscordNotifier.cpp
  ConfigPortal/
    ConfigPortal.h
    ConfigPortal.cpp
  ConfigManager/
    ConfigManager.h
    ConfigManager.cpp
  SettingsStore/
    SettingsStore.h
    SettingsStore.cpp
  SessionAuth/
    SessionAuth.h
    SessionAuth.cpp
  DashboardServer/
    DashboardServer.h
    DashboardServer.cpp
  LogManager/
    LogManager.h
    LogManager.cpp

include/
  config.h
  pins.h
  states.h
  messages.h
```

---

## 17. 模組職責

| 模組 | 職責 | 禁止 |
|------|------|------|
| FaceRecognizer | ROI 擷取、feature vector、texture validation、cosine similarity、NVS 人臉管理 | 警報決策 |
| FaceVoter | temporal voting、KNOWN/UNKNOWN confirmed、idle reset | 影像特徵萃取 |
| CameraAgent | camera init、MJPEG stream、frame capture、enroll 排程 | 辨識決策 |
| DoorSensor | 霍爾感應器 ADC 讀取、去彈跳、DoorState 輸出 | 警戒決策 |
| LedController | RGB LED 狀態控制（常亮/閃爍）| 決策邏輯 |
| BuzzerController | 蜂鳴器開關、pattern | 決策邏輯 |
| SecurityStateMachine | AlertLevel 計算、已知使用者事件、unknown alert、alarm decision 處理 | 影像處理 |
| AgentComm | MQTT publish/subscribe、presence 與 alarm_decision 接收 | 決策邏輯 |
| DiscordNotifier | HTTPS webhook、TLS、rate limit、failure cooldown | 阻塞主流程 |
| ConfigPortal | AP mode、WiFi 首次設定 | 其他設定 |
| ConfigManager | MQTT 設定、face threshold、FaceVoter 參數的 NVS 讀寫 | WiFi 設定 |
| SettingsStore | dashboard_pw_hash、discord_url 的 NVS 讀寫 | WiFi 設定 |
| SessionAuth | session token、cookie、TTL、brute-force throttle、CSRF | 業務邏輯 |
| DashboardServer | HTTP routes、PROGMEM HTML、AJAX API | 核心決策邏輯 |
| LogManager | Face/Door/Alert log 的 in-memory ring buffer 與 JSON 序列化 | 警戒決策 |

---

## 18. 錯誤處理規則

| 錯誤 | 處理方式 |
|------|----------|
| WiFi 失敗 | 不停止本機警戒；可進入 AP mode；重連後恢復 Discord 與 AgentComm |
| NTP 失敗 | 系統繼續運作；log 標記 `time_unsynced` |
| Camera 失敗 | 不 crash；face state 設為 FACE_NO_FACE；持續嘗試恢復 |
| Agent 2 離線 | 預設 ALERT_RED；Agent 1 獨立警戒 |
| MQTT 斷線 | 不停止本機警戒；背景重連；重連後重新 subscribe |
| Discord 失敗 | 不阻塞主流程；記錄失敗狀態；5 min cooldown |

---

## 19. 不可違反規則

- 不可用單一 frame 直接觸發事件
- 不可略過 FaceVoter
- 不可略過 texture validation
- 不可讓 WebUI 成為核心決策邏輯
- 不可依賴 Agent 2 才能警戒
- 不可因 WiFi / Discord / NTP / MQTT 失敗停止本機功能
- 不可在 ESP32 主流程加入大型 CNN

---

## 20. 計時常數（config.h）

| 常數 | 值 | 說明 |
|------|-----|------|
| `FACE_VOTE_WINDOW_MS` | 10000 | UNKNOWN_CONFIRMED 最短持續時間 |
| `FACE_VOTE_IDLE_MS` | 5000 | 無臉 idle reset 時間 |
| `FACE_VOTE_KNOWN_MIN` | 3 | KNOWN_CONFIRMED 最少 hit 數 |
| `FACE_VOTE_KNOWN_WINDOW_MS` | 8000 | KNOWN hit 累積時間窗口 |
| `FACE_VOTE_UNKNOWN_MIN_HITS` | 10 | UNKNOWN_CONFIRMED 最少 frame 數 |
| `FACE_SIMILARITY_THRESHOLD` | 0.92 | cosine similarity 門檻 |
| `FACE_TEXTURE_MIN_STDDEV` | 20.0 | texture 最低 stddev |
| `DOOR_DEBOUNCE_MS` | 200 | 霍爾感應器去彈跳 |
| `WIFI_CONNECT_TIMEOUT_MS` | 15000 | STA 連線逾時 |
| `PORTAL_TIMEOUT_MS` | 300000 | AP mode 無 POST → restart |
| `WIFI_LOST_TIMEOUT_MS` | 300000 | 持續斷線 → restart to portal |
| `DISCORD_RATE_LIMIT_MS` | 30000 | 同 state 最短通知間隔 |
| `DISCORD_TIMEOUT_MS` | 5000 | webhook 連線逾時 |
| `DISCORD_FAIL_COOLDOWN_MS` | 300000 | 連線失敗後 cooldown |
| `DASHBOARD_SESSION_TTL_MS` | 1800000 | session 30 min TTL |
| `LOGIN_LOCKOUT_MS` | 60000 | 暴力破解 lockout 60 s |
| `LOGIN_MAX_FAILS` | 5 | 最大失敗次數 |
| `PEER_QUERY_INTERVAL_MS` | 5000 | Agent 2 狀態輪詢間隔（HTTP fallback） |
| `CAMERA_DETECT_INTERVAL_MS` | 500 | 人臉偵測頻率 |

---

## 21. Logging Convention

```
[Agent1] boot
[Agent1] WiFi connected. IP: 192.168.x.x
[Agent1] face KNOWN_CONFIRMED: Alice (sim=0.95)
[Agent1] face UNKNOWN_CONFIRMED
[Agent1] door OPEN
[Agent1] alert RED: buzzer + discord
[Agent1] alarm_decision: CANCEL_ALARM (from Agent2)
[Agent1] discord sent (last 8: xxxxxxxx)
[Agent1] WARNING: Agent2 offline — default ALERT_RED
```
