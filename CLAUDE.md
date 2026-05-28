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
- RGB LED（WS2812B 單顆可定址 NeoPixel，GPIO 32）
- 蜂鳴器
- Discord Webhook
- Local WebUI

---

## 3. GPIO 配置

**Avoid:** GPIO 0, 2, 5, 6–11, 12, 18–19, 21–23, 25–27, 34–36, 39（Camera 或 boot-strap）  
**Safe:** GPIO 13, 32, 33

**GPIO 34, 35, 36, 39 是 input-only，無內部上拉電阻。**  
**GPIO 25 / 26 / 27** 被 Camera 佔用（VSYNC / SIOD / SIOC），不可作為一般 GPIO。  
**GPIO 32**：NMK99 上 CAM_PWDN 未接線，`pin_pwdn = -1`，因此 GPIO 32 可用作 WS2812B 資料線。

```
PIN_LED_DATA = 32   // WS2812B 單顆 NeoPixel 資料線（NMK99 板載 LED）
PIN_BUZZER   = 13   // 蜂鳴器（被動壓電式，LEDC PWM Channel 7）
PIN_HALL     = 33   // 霍爾感應器（ADC1_CH5，WiFi 啟動後仍可使用）
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

使用 **HOG-lite** 輕量梯度方向直方圖演算法，不使用大型 CNN。

**流程：**

```
Camera frame（YUV422，QVGA 320×240）
  ↓
frame 格式驗證（非 JPEG、有效解析度、PSRAM 存在）
  ↓
擷取中央 60% ROI（W/5..W*4/5, H/5..H*4/5）
  ↓
切成 4×4 grid = 16 cells
每個 cell 計算 4-bin 無符號梯度方向直方圖（HOG-lite）：
  中央差分：dx = Y(x+1)-Y(x-1), dy = Y(y+1)-Y(y-1)（Y channel only）
  ax = |dx|, ay = |dy|；mag < 2 → 忽略（noise floor）
  sameSign = (dx≥0)==(dy≥0)
  ax >= ay → (sameSign ? bin 0 : bin 1)（水平主導）
  ay >  ax → (sameSign ? bin 2 : bin 3)（垂直主導）
→ 16 cells × 4 bins = 64 維 raw feature vector
  ↓
Per-cell L1 normalization → 統計 activeCells（cellEnergy/cellPix > 0.5f）
Active-cells gate（activeCells < 4 → _extract() 回傳 0.f → NO_FACE）
  ↓
Global L2 normalization → 64-float unit vector
  ↓
Texture validation（totalEnergy/totalPix < FACE_TEXTURE_MIN_STDDEV (12.0) → 回傳 NO_FACE）
  ↓
是否有已註冊人臉？（_n == 0 → 回傳 NO_FACE）
  ↓
Per-user best-template cosine similarity（每位使用者最多 MAX_TEMPLATES_PER_USER = 5 個模板）
取最高 similarity 的 user（bestSim）與第二高（secondSim）
  ↓
Margin check（≥ 2 人時：bestSim - secondSim < FACE_MARGIN_MIN (0.03) → UNKNOWN）
  ↓
Similarity ≥ FACE_SIMILARITY_THRESHOLD (0.90) → FACE_KNOWN
否則 → FACE_UNKNOWN
```

**規則：**
- 只處理 YUV422（YUYV）的 Y channel（byte index = pixel_index × 2）
- ROI 取中央 60%（各方向各取 1/5 邊距）
- Feature vector 為 **64 維**（4×4 cells × 4 orientation bins）
- 每位使用者最多 **5 個模板**（`MAX_TEMPLATES_PER_USER = 5`）；同名 enroll 追加模板而非覆蓋
- 最多支援 7 位使用者（`MAX_FACES = 7`）
- 人臉資料儲存在 NVS（key: `face_feat`, `face_cnt`, `face_names`, `face_tcnt`）
- Similarity ≥ `FACE_SIMILARITY_THRESHOLD` (0.90) 且 margin ≥ `FACE_MARGIN_MIN` (0.03) → FACE_KNOWN
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
| KNOWN_CONFIRMED（`KNOWN_GREEN_DURATION_MS` = 60s 窗口，無告警） | ALERT_GREEN |

### LED 顯示優先級（updateLed() 在 main.cpp 統一管理）

| 優先級 | 條件 | LED 狀態 |
|--------|------|---------|
| 1（最高） | `isAlarmActive()` | 紅色閃爍（250ms） |
| 2 | `ALERT_GREEN` | 綠色常亮 |
| 3 | 偵測到任意人臉（DETECTED / KNOWN / UNKNOWN） | 白色常亮（fill light） |
| 4（最低） | 無任何上述條件 | 關閉 |

### Red Alert 行為

- RGB LED 亮紅色（idle）
- 若收到 `UNKNOWN_CONFIRMED`：
  - LED 紅色閃爍
  - 蜂鳴器警示（持續 `BUZZER_DURATION_MS` 後呼叫 `_cancelAlarm()`，完整取消警報）
  - 發送 Discord webhook：`notifyWithPhoto()`（含相片）；`jpegBuf == nullptr` 或 multipart body 記憶體失敗時 fallback 純文字 `notify()`（HTTP 失敗不 fallback）
  - 記錄警戒事件至 Alert Log

### Yellow Alert 行為

- RGB LED 亮紅色（idle；YELLOW 在 LED 上與 idle RED 相同）
- 若收到 `UNKNOWN_CONFIRMED`：
  - 通知 Agent 2（MQTT publish `home/security/alert`）
  - 等待 Agent 2 回傳 `alarm_decision`（逾時 `ALARM_DECISION_TIMEOUT_MS` = 30s → 預設 TRIGGER_ALARM）
  - `TRIGGER_ALARM` → 啟動蜂鳴器 + LED 紅色閃爍
  - `CANCEL_ALARM` → 取消警戒並記錄

### Green / Normal 行為

- RGB LED 亮綠色（60s 窗口內）
- KNOWN_CONFIRMED 時記錄 Face Log、Door Log（若門已開）、Discord 通知（已知使用者回家）

### 警報自動取消條件（任一觸發即取消）

| 條件 | 說明 |
|------|------|
| 蜂鳴器持續時間結束 | `BUZZER_DURATION_MS`（預設 60s）後呼叫 `_cancelAlarm()`，完整取消警報 |
| 門關閉時 | `onDoorChange(DOOR_CLOSED)` 在警報中 → 呼叫 `_cancelAlarm()` |
| KNOWN_CONFIRMED 收到時 | `onVoteResult(KNOWN_CONFIRMED)` 在警報中 → 呼叫 `_cancelAlarm()` |

> `_silenceBuzzer()`（僅靜音）與 `_cancelAlarm()`（完整取消）是兩個不同操作。

### 其他事件輸入與邊界行為

- **`onFaceKnownRaw(name)`**：每 loop() 在 raw KNOWN 時呼叫，更新 `_lastSeenKnownName` 與時間戳；DOOR_OPEN 時若 KNOWN_GREEN 窗口已過期但臉仍在畫面中，仍可用此歸因使用者
- **`onAgent2Online(false)`**：若正在等待 Agent 2 決策（`_waitingForDecision`），Agent 2 離線直接觸發 `TRIGGER_ALARM`，不等 timeout
- **`onAlarmDecision(TRIGGER_ALARM)`**：只在 `_waitingForDecision` 狀態下生效，防止 retained/replayed MQTT 訊息影響
- **`onDoorChange(DOOR_OPEN)`**：套用 pending 或 last-seen known user 歸因，`_returnFired` 旗標防止同一段綠燈窗口內重複觸發「已知使用者回家」事件

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

**實際路由（與程式碼同步）：**

| 路徑 | Method | Auth | CSRF | 說明 |
|------|--------|------|------|------|
| `/` | GET | — | — | redirect → `/dashboard` |
| `/login` | GET/POST | — | — | 登入（SessionAuth 管理） |
| `/logout` | GET | session | — | 登出 |
| `/password/change` | GET | session | — | 首次登入強制密碼修改頁 |
| `/password/save` | POST | session | ✓ | 儲存新密碼 |
| `/dashboard` | GET | session + 改密 | — | 即時狀態（3s AJAX）+ Camera preview + 人臉管理 |
| `/settings` | GET | session + 改密 | — | **統一設定頁**（Discord、MQTT、霍爾、蜂鳴器、密碼） |
| `/settings/save` | POST | session + 改密 | ✓ | 儲存設定 |
| `/log/door` | GET | session + 改密 | — | 門禁紀錄頁面（含 RAM + SPIFFS 月份切換） |
| `/log/face` | GET | session + 改密 | — | 人臉辨識紀錄頁面 |
| `/log/alert` | GET | session + 改密 | — | 警戒事件紀錄頁面 |
| `/analytics` | GET | session + 改密 | — | 統計分析頁面（門次、面孔排行、峰值時段） |
| `/api/status` | GET | session + 改密 | — | JSON 即時狀態（見下方欄位） |
| `/api/log/door` | GET | session + 改密 | — | 門禁紀錄 JSON（RAM 最近 50 筆） |
| `/api/log/face` | GET | session + 改密 | — | 人臉辨識紀錄 JSON |
| `/api/log/alert` | GET | session + 改密 | — | 警戒事件紀錄 JSON |
| `/api/log/door/paged` | GET | session + 改密 | — | SPIFFS 分頁（?month=YYYYMM&page=N&per_page=20） |
| `/api/log/face/paged` | GET | session + 改密 | — | SPIFFS 分頁 |
| `/api/log/alert/paged` | GET | session + 改密 | — | SPIFFS 分頁 |
| `/api/log/stats` | GET | session + 改密 | — | 月份統計 JSON（?month=YYYYMM） |
| `/api/log/months` | GET | session + 改密 | — | 有資料月份列表 JSON |
| `/api/face/enroll` | POST | session + 改密 | ✓ | 觸發人臉註冊（form-urlencoded: csrf + name） |
| `/api/face/list` | GET | session + 改密 | — | 已註冊人臉名稱列表 JSON |
| `/api/face/clear` | POST | session + 改密 | ✓ | 清除所有人臉資料 |
| `/api/buzzer/test` | POST | session | ✓ | 測試蜂鳴器（?freq= 可選） |

> WiFi 設定**不**在 WebUI，只能透過 Config Portal（AP 模式）設定。  
> `session + 改密` = 需 session 且已修改預設密碼，否則 redirect `/password/change`。

**設計限制：**
- HTML 儲存於 PROGMEM `const char[]`，無 LittleFS
- 無外部 CDN，無 React/Vue
- `/api/status` 欄位包含：`alert_level`, `door_state`, `agent2_online`, `alarm_active`, `last_known_user`, `presence_state`, `uptime`, `hall_raw`, `hall_lower`, `hall_upper`, `face_count`, `face_max`, `face_result`, `face_name`, `face_sim`, `face_tex`, `face_voter_*`
- 使用 ArduinoJson `JsonDocument`（非固定大小 StaticJsonDocument）

---

## 12. Session Auth

- Username: `admin`（hardcoded）
- Password: salted SHA-256 hex 存於 NVS key `dashboard_pw`；預設密碼強制變更
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
| `dashboard_pw` | SettingsStore | String | salted SHA-256 hex（NVS key 為 `dashboard_pw`） |
| `pw_changed` | SettingsStore | Bool | 是否已修改過預設密碼 |
| `discord_url` | SettingsStore | String | `https://discord.com/api/webhooks/` prefix; max 256 chars |
| `hall_lo` | SettingsStore | UInt32 | 霍爾感應器 open zone 下界；預設 1000 |
| `hall_hi` | SettingsStore | UInt32 | 霍爾感應器 open zone 上界；預設 3000 |
| `mqtt_broker` | ConfigManager | String | MQTT broker IP/hostname; max 64 chars |
| `mqtt_port` | ConfigManager | UInt16 | default 1883 |
| `buzzer_freq` | ConfigManager | UInt32 | 蜂鳴器頻率 Hz；default 2000；range 200–8000 |
| `buzzer_dur` | ConfigManager | UInt32 | 蜂鳴器持續時間 ms；default 60000；range 10000–300000 |
| `face_feat` | FaceRecognizer | Blob | 最大 7×5×64×4 = 8960 bytes (float32)；實際寫入量 = enrolled_count × MAX_TEMPLATES_PER_USER × FEATURE_DIM × 4 |
| `face_cnt` | FaceRecognizer | UInt8 | enrolled user count; 0 = none |
| `face_names` | FaceRecognizer | Blob | MAX_FACES × (MAX_NAME_LEN+1) = 7×17 = 119 bytes |
| `face_tcnt` | FaceRecognizer | Blob | per-user template counts; MAX_FACES bytes |

---

## 14. Discord Webhook 通知

```cpp
// Returns true if sent, false if rate-limited, cooldown, invalid URL, or error.
// event: AlertEvent 用於 per-event rate limiting（取代舊 SystemState）
static bool DiscordNotifier::notify(const String& webhookUrl, AlertEvent event,
                                    const String& message);

// Send with JPEG photo attachment（multipart/form-data）
// jpegBuf 所有權留呼叫者；不釋放。jpegBuf=nullptr 或 multipart body 記憶體失敗時
// fallback 為純文字 notify()；HTTP POST 失敗不會 fallback。
static bool DiscordNotifier::notifyWithPhoto(const String& webhookUrl, AlertEvent event,
                                              const String& message,
                                              const uint8_t* jpegBuf, size_t jpegLen);

// One-shot boot notification; 不更新 _failCooldownUntil 或 _lastNotifyMs
static bool DiscordNotifier::notifyBoot(const String& webhookUrl, const String& message);
```

**AlertEvent enum（states.h）：**

```cpp
enum class AlertEvent { UNKNOWN_VISITOR = 0, USER_RETURNED = 1, BOOT = 2 };
```

**觸發事件：**
- `UNKNOWN_CONFIRMED`（Red Alert）→ `notifyWithPhoto()`（含相片）；`jpegBuf == nullptr` 時 fallback `notify()`
- Known user 回家（KNOWN_CONFIRMED + 門開啟） → `notify(AlertEvent::USER_RETURNED)`
- 開機 IP 公告 → `notifyBoot()`

**設計規則：**
- TLS: `WiFiClientSecure` with CA root cert（`DISCORD_TLS_INSECURE` flag 才能 setInsecure）；需在 `platformio.ini` 定義其中一個，否則編譯失敗
- URL whitelist: `https://discord.com/api/webhooks/` 或 `https://discordapp.com/api/webhooks/`
- Timeout: 5 s（connect + read）
- Rate limit: 同 AlertEvent 最少間隔 30 s（各事件獨立計時）
- Fail cooldown: 連線失敗後 5 min 封鎖所有通知；`notifyBoot()` 不受此影響
- Non-blocking: 所有前置檢查（`_canSend()`）在 network I/O 前完成
- Log masking: 只印出 webhook URL 最後 8 字元

---

## 15. Log 系統（LogManager）

RAM 環形緩衝區各保留最近 **50 筆**；同時支援 **SPIFFS 持久化儲存**（月份分檔 NDJSON）。

**Storage tiers：**
- **RAM ring buffer**（即時查詢）：最近 50 筆，`getFaceLogJson()` 等 API
- **SPIFFS 持久化**（歷史查詢）：月份分檔 `/face_YYYYMM.ndjson`、`/door_YYYYMM.ndjson`、`/alert_YYYYMM.ndjson`（SPIFFS 根目錄）；`beginSpiffs()` 啟用；NTP 未同步時略過寫入

**查詢 API：**
- `getXxxLogJson()` — RAM ring（最近 50 筆）
- `getXxxLogPagedJson(month, page, perPage)` — SPIFFS 分頁（`{total, page, per_page, data[]}`）
- `getStatsJson(month)` — 月份統計（today/week/month count、daily_week[7]）
- `getAvailableMonthsJson()` — 已有資料的月份列表（降序）

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
| ConfigManager | MQTT 設定、蜂鳴器頻率與持續時間的 NVS 讀寫 | WiFi 設定 |
| SettingsStore | dashboard_pw、pw_changed、discord_url、霍爾感應器 hall_lo/hall_hi 的 NVS 讀寫 | WiFi 設定 |
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

| 常數 | 值 | 來源 | 說明 |
|------|-----|------|------|
| `FACE_VOTE_WINDOW_MS` | 10000 | config.h | UNKNOWN_CONFIRMED 最短持續時間 |
| `FACE_VOTE_IDLE_MS` | 5000 | config.h | 無臉 idle reset 時間 |
| `FACE_VOTE_KNOWN_MIN` | 3 | config.h | KNOWN_CONFIRMED 最少 hit 數 |
| `FACE_VOTE_KNOWN_WINDOW_MS` | 8000 | config.h | KNOWN hit 累積時間窗口 |
| `FACE_VOTE_UNKNOWN_MIN_HITS` | 10 | config.h | UNKNOWN_CONFIRMED 最少 frame 數 |
| `FACE_SIMILARITY_THRESHOLD` | **0.90** | config.h | cosine similarity 門檻 |
| `FACE_MARGIN_MIN` | **0.03** | config.h | 第二高 user 最低分差（防誤認） |
| `FACE_TEXTURE_MIN_STDDEV` | **12.0** | config.h | mean L1 gradient per pixel 最低值 |
| `FACE_RECENT_MS` | 10000 | config.h | 視為「當前有臉」的時間窗口 |
| `DOOR_DEBOUNCE_MS` | 200 | config.h | 霍爾感應器去彈跳 |
| `HALL_DEFAULT_LOWER` | **1000** | config.h | 霍爾 open zone 下界預設 |
| `HALL_DEFAULT_UPPER` | **3000** | config.h | 霍爾 open zone 上界預設 |
| `HALL_HYSTERESIS` | 150 | config.h | 死區半寬（各邊界各側） |
| `HALL_SAMPLE_INTERVAL_MS` | 50 | config.h | ADC 採樣頻率 |
| `WIFI_CONNECT_TIMEOUT_MS` | 15000 | config.h | STA 連線逾時 |
| `PORTAL_TIMEOUT_MS` | 300000 | config.h | AP mode 無 POST → restart |
| `WIFI_LOST_TIMEOUT_MS` | 300000 | config.h | 持續斷線 → restart to portal |
| `MQTT_DEFAULT_PORT` | 1883 | config.h | MQTT 預設埠號 |
| `MQTT_RECONNECT_MS` | 5000 | config.h | MQTT 重連間隔 |
| `MQTT_KEEPALIVE_S` | 60 | config.h | MQTT Keep-alive |
| `AGENT2_OFFLINE_TIMEOUT_MS` | 15000 | config.h | 無 presence → Agent 2 離線 |
| `DISCORD_RATE_LIMIT_MS` | 30000 | config.h | 同 AlertEvent 最短通知間隔 |
| `DISCORD_TIMEOUT_MS` | 5000 | config.h | webhook 連線逾時 |
| `DISCORD_FAIL_COOLDOWN_MS` | 300000 | config.h | 連線失敗後 cooldown |
| `BUZZER_DEFAULT_FREQ_HZ` | **2000** | config.h | 蜂鳴器預設頻率（Hz） |
| `BUZZER_DURATION_MS` | **60000** | config.h | 警報蜂鳴器自動靜音時間 |
| `BUZZER_TEST_DURATION_MS` | **500** | config.h | 測試嗶聲持續時間 |
| `DASHBOARD_SESSION_TTL_MS` | 1800000 | config.h | session 30 min TTL |
| `LOGIN_LOCKOUT_MS` | 60000 | config.h | 暴力破解 lockout 60 s |
| `LOGIN_MAX_FAILS` | 5 | config.h | 最大失敗次數 |
| `CAMERA_DETECT_INTERVAL_MS` | 500 | config.h | 人臉偵測頻率 |
| `CAMERA_ENROLL_TIMEOUT_MS` | 10000 | config.h | Enroll 等待逾時 |
| `ALARM_DECISION_TIMEOUT_MS` | 30000 | SecurityStateMachine.h | Yellow alert 等待 Agent 2 決定逾時 |
| `KNOWN_GREEN_DURATION_MS` | 60000 | SecurityStateMachine.h | KNOWN_CONFIRMED 保持 GREEN 的時間窗口 |

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
