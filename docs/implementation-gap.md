# Agent 1 重構差異分析與待實作清單

本文件根據新版 CLAUDE.md 規格，列出現有程式碼與目標架構的差距。  
分為三類：**可直接沿用**、**需修改**、**需從頭實作**。

---

## 一、可直接沿用（基本無需改動）

| 模組 | 路徑 | 說明 |
|------|------|------|
| FaceRecognizer | `lib/FaceRecognizer/` | 32-dim feature vector、NVS 儲存、cosine similarity、texture validation、最多 7 人 — 完整符合規格 |
| FaceVoter | `lib/FaceVoter/` | temporal voting、KNOWN_CONFIRMED、UNKNOWN_CONFIRMED、idle reset、`FaceVoterStatus` — 完整符合規格 |
| CameraAgent | `lib/CameraAgent/` | camera init、MJPEG stream（port 81）、frame capture、enroll 排程 — 功能完整 |
| ConfigPortal | `lib/ConfigPortal/` | AP mode、WiFi 首次設定、NVS wifi_ssid/wifi_pw、5 min timeout — 完整符合規格 |
| SettingsStore | `lib/SettingsStore/` | discord_url、dashboard_pw_hash、NVS key 長度限制、URL whitelist — 完整符合規格 |
| SessionAuth | `lib/SessionAuth/` | session token、cookie、30 min TTL、brute-force throttle (5→60s)、CSRF — 完整符合規格 |
| DiscordNotifier | `lib/DiscordNotifier/` | TLS、URL whitelist、rate limit、failure cooldown、notifyBoot — 完整符合規格 |

---

## 二、需修改的現有模組

### 2.1 `platformio.ini`

**問題：** 目前定義兩個 env（`indoor`, `outdoor`）。  
**修改：** 改為單一 env `agent1`，移除 `indoor` 環境及所有 `INDOOR_AGENT` 相關 build_flag。

```ini
[env:agent1]
build_flags      = -I include -D AGENT1
build_src_filter = -<*> +<main.cpp>
```

---

### 2.2 `include/states.h`

**問題：** 目前只有 `SystemState` enum（混合了 indoor/outdoor 狀態）。  
**修改：** 保留 `StateEvent` 結構，新增以下 enum，移除舊的 `SystemState` 或保留作為 legacy（視 DoorStateMachine 重構進度而定）：

```cpp
enum class DoorState { DOOR_CLOSED, DOOR_OPEN };
enum class FaceState { FACE_NO_FACE, FACE_KNOWN, FACE_UNKNOWN };
enum class VoteResult { NONE, KNOWN_CONFIRMED, UNKNOWN_CONFIRMED };  // 已存在於 FaceVoter.h，移至此處統一
enum class AlertLevel { ALERT_GREEN, ALERT_YELLOW, ALERT_RED };
enum class AlarmDecision { NO_ACTION, TRIGGER_ALARM, CANCEL_ALARM };
```

---

### 2.3 `include/pins.h`

**問題：** 目前分 `INDOOR_AGENT` / else 兩條路，且 LED 只有單色（PIN 33）。  
**修改：** 移除 `#ifdef INDOOR_AGENT` 分支，改為單一 Agent 1 配置，新增 RGB LED pins：

```cpp
#define PIN_LED_R   25
#define PIN_LED_G   26
#define PIN_LED_B   27
#define PIN_BUZZER  13
#define PIN_HALL    33   // ADC1_CH5，霍爾感應器
```

---

### 2.4 `include/config.h`

**問題：** 包含 indoor 專用常數（`HALL_DEFAULT_THRESHOLD`, `HALL_HYSTERESIS`, `HALL_SAMPLE_INTERVAL_MS`）及使用 mDNS Indoor/Outdoor 命名。  
**修改：**
- 保留 Hall sensor 常數（Agent 1 現在要用霍爾感應器）
- 將 `MDNS_INDOOR` / `MDNS_OUTDOOR` 改為 `MDNS_AGENT1` / `MDNS_AGENT2`
- 新增 MQTT 相關常數：

```cpp
#define MQTT_DEFAULT_PORT     1883
#define MQTT_RECONNECT_MS     5000UL
#define MQTT_KEEPALIVE_S        60
#define AGENT2_OFFLINE_TIMEOUT_MS  10000UL  // 超過此時間無 presence → 視為離線
```

---

### 2.5 `lib/DashboardServer/`

**問題：** 目前 API 缺少 Log 查詢路由，`/api/status` 未包含 `alert_level`、`presence_state`、`door_state` 等新欄位；WebUI 頁面缺少 Face Register、Camera Preview、Door Log、Face Log、Alert Log。  
**修改：**
- `begin()` 簽名新增 `LogManager*` 參數
- `/api/status` JSON 加入：`alert_level`, `presence_state`, `agent2_online`, `door_state`
- 新增路由：`/log/door`, `/log/face`, `/log/alert`（回傳 JSON 陣列）
- 新增頁面：`/face/register`（含 enroll 按鈕與 camera preview iframe）
- `/settings` 拆分為 `/settings/wifi`, `/settings/discord`, `/settings/system`

---

### 2.6 `src/outdoor_main.cpp` → `src/main.cpp`

**問題：** 檔案名稱、agent 標籤、架構均需全面改寫。  
**修改：**
- 重新命名為 `main.cpp`
- 移除對 `AgentProtocol`（HTTP REST peer query）的依賴，改用 `AgentComm`（MQTT）
- 移除 `DoorStateMachine`，改用 `SecurityStateMachine`
- 新增 `DoorSensor`、`LedController`、`BuzzerController`、`LogManager` 的整合
- `updateActuators()` 改由 `LedController` 與 `BuzzerController` 根據 `AlertLevel` 控制
- Serial 指令更新：新增 `d`（door toggle for testing）、`l`（show logs）

---

### 2.7 `lib/AgentProtocol/` → 重構為 `lib/AgentComm/`

**問題：** 目前使用 HTTP REST 查詢 peer；新規格改用 MQTT。  
**修改：** 可保留 HTTP REST 作為 fallback（`/api/status` 查詢），但主要通訊改用 MQTT。  
若完整重寫，移除 `AgentProtocol` lib，新建 `AgentComm`（見第三節）。

---

### 2.8 `lib/DoorStateMachine/` → 重構為 `lib/SecurityStateMachine/`

**問題：** 目前 `DoorStateMachine` 處理 indoor 的出入門邏輯（`LEAVING_HOME`, `ENTERING_HOME` 等），與新規格的安全警戒邏輯不同。  
**修改：** 建立 `SecurityStateMachine` 取代，內部狀態改以 `AlertLevel` 為主，整合 `DoorState`、`VoteResult`、`AlarmDecision`。舊的 `DoorStateMachine` 可暫時保留但停止使用。

---

## 三、需從頭實作的模組

### 3.1 `lib/DoorSensor/`

負責霍爾感應器 ADC 讀取與 DoorState 輸出。

```cpp
class DoorSensor {
public:
  static void begin(uint8_t pin, uint16_t threshold, uint16_t hysteresis);
  static void tick();                     // 在 loop() 呼叫
  static DoorState getState();
  static uint16_t  getRaw();
  static void      setThreshold(uint16_t t);
  static void      setOnChange(void (*cb)(DoorState));
};
```

**實作要點：**
- ADC1 channel（GPIO 33），支援 WiFi 同時使用
- 去彈跳：`DOOR_DEBOUNCE_MS` (200 ms)
- threshold + hysteresis 防止在臨界值附近抖動
- 狀態改變時呼叫 callback

---

### 3.2 `lib/LedController/`

負責 RGB LED 依 AlertLevel 顯示對應顏色與閃爍。

```cpp
class LedController {
public:
  static void begin(uint8_t pinR, uint8_t pinG, uint8_t pinB);
  static void setLevel(AlertLevel level);
  static void setBlinking(bool enable, uint16_t periodMs = 250);
  static void tick();  // 在 loop() 呼叫處理閃爍
};
```

**顏色對應：**
- `ALERT_GREEN` → 綠色常亮
- `ALERT_YELLOW` → 黃色常亮
- `ALERT_RED` 平時 → 紅色常亮
- `ALERT_RED` + UNKNOWN_CONFIRMED → 紅色閃爍

---

### 3.3 `lib/BuzzerController/`

負責蜂鳴器警示邏輯。

```cpp
class BuzzerController {
public:
  static void begin(uint8_t pin);
  static void trigger();   // 啟動警示
  static void cancel();    // 停止
  static bool isActive();
};
```

---

### 3.4 `lib/SecurityStateMachine/`

核心警戒決策邏輯，取代 `DoorStateMachine`。

```cpp
class SecurityStateMachine {
public:
  void setPresence(bool occupied);       // 來自 AgentComm
  void setAgent2Online(bool online);
  void onVoteResult(VoteResult result);
  void onDoorChange(DoorState state);
  void onAlarmDecision(AlarmDecision d); // 來自 AgentComm
  AlertLevel getAlertLevel() const;
  DoorState  getDoorState() const;
  FaceState  getFaceState() const;
  const char* getLastKnownUser() const;
  void tick();
};
```

**決策規則（參見 CLAUDE.md 第 7 節）：**
- Agent 2 離線 OR presence = UNOCCUPIED → ALERT_RED
- presence = OCCUPIED → ALERT_YELLOW
- KNOWN_CONFIRMED（無告警觸發）→ ALERT_GREEN（可選）

---

### 3.5 `lib/AgentComm/`

MQTT 通訊層，取代 `AgentProtocol`（HTTP REST）。

```cpp
class AgentComm {
public:
  static void begin(const char* broker, uint16_t port);
  static void tick();  // 在 loop() 呼叫，處理 MQTT loop 與重連

  // Publish
  static void publishDoorEvent(DoorState state, const char* user = nullptr);
  static void publishFaceEvent(const char* userName, float similarity);
  static void publishAlertEvent(AlertLevel level, const char* type);
  static void publishStatus(AlertLevel level, unsigned long uptime);

  // Callbacks (set before begin)
  static void setOnPresence(void (*cb)(bool occupied, int score));
  static void setOnAlarmDecision(void (*cb)(AlarmDecision decision));
};
```

**實作要點：**
- 使用 `PubSubClient` 或 ESP32 內建 MQTT library
- MQTT broker IP 由 `ConfigManager` 讀取
- 斷線後背景自動重連（`MQTT_RECONNECT_MS`）
- Subscribe 在連線後立即重新訂閱（防止重連後 subscription 消失）
- `AGENT2_OFFLINE_TIMEOUT_MS` 內無 presence message → 視為 Agent 2 離線

---

### 3.6 `lib/ConfigManager/`

管理非 WiFi、非 Discord 的可設定參數。

```cpp
class ConfigManager {
public:
  static void begin();
  static String getMqttBroker();
  static uint16_t getMqttPort();
  static float getFaceSimilarityThreshold();
  static uint16_t getHallThreshold();
  static void save(const String& mqttBroker, uint16_t mqttPort,
                   float faceThreshold, uint16_t hallThreshold);
};
```

---

### 3.7 `lib/LogManager/`

In-memory ring buffer log，提供 JSON 序列化供 WebUI 查詢。

```cpp
class LogManager {
public:
  static void begin();
  static void logFace(FaceState state, VoteResult vote,
                      const char* userName, float similarity);
  static void logDoor(DoorState state, const char* relatedUser = nullptr);
  static void logAlert(AlertLevel level, const char* alertType,
                       AlarmDecision decision, bool discordResult);

  // 回傳 JSON 陣列字串（用於 WebUI /log/* 路由）
  static String getFaceLogJson();
  static String getDoorLogJson();
  static String getAlertLogJson();
};
```

**實作要點：**
- 每類 log 各獨立 ring buffer（最多 50 筆）
- 時間戳來自 NTP（若未同步則標記 `time_unsynced`）
- 序列化使用 `ArduinoJson`
- 不使用 LittleFS（純記憶體，重開機後清空）

---

## 四、需刪除 / 停用的現有程式碼

| 路徑 | 原因 |
|------|------|
| `src/indoor_main.cpp` | Indoor agent 不在此專案範圍內 |
| `lib/DoorStateMachine/` | 被 `SecurityStateMachine` 取代 |
| `lib/AgentProtocol/` | 被 `AgentComm`（MQTT）取代；若保留 HTTP fallback 可保留部分程式 |
| `include/states.h` 中的 `SystemState` enum | 被新的多個 enum 取代（`DoorState`, `AlertLevel` 等） |

---

## 五、實作順序建議

優先確保可獨立運作的核心功能穩定，再加入 Agent 2 通訊。

```
Phase 1：硬體基礎（不依賴 WiFi）
  ├─ 修改 pins.h → RGB LED 三色
  ├─ 實作 LedController
  ├─ 實作 BuzzerController
  └─ 實作 DoorSensor（霍爾感應器）

Phase 2：WiFi 與設定
  ├─ ConfigPortal（已存在，確認 AP 名稱改為 "Agent1-Setup"）
  ├─ 實作 ConfigManager（MQTT broker 等參數）
  └─ 確認 SettingsStore 無需改動

Phase 3：警戒狀態機
  ├─ 更新 states.h（新增所有 enum）
  ├─ 實作 SecurityStateMachine
  └─ 整合 DoorSensor → SecurityStateMachine

Phase 4：Log 系統
  └─ 實作 LogManager

Phase 5：Dashboard 擴充
  ├─ 更新 DashboardServer 路由與 JSON
  └─ 新增 WebUI 頁面（Door/Face/Alert Log, Face Register）

Phase 6：Agent 2 通訊
  └─ 實作 AgentComm（MQTT）

Phase 7：main.cpp 重寫
  └─ 整合所有模組，移除 indoor 相關程式碼

Phase 8：Camera + Face Recognition（Phase 4-5 已穩定後）
  ├─ CameraAgent（已存在）
  ├─ FaceRecognizer（已存在）
  └─ FaceVoter（已存在）
```

---

## 六、風險提示

1. **MQTT library 選擇**：ESP32 Arduino 生態主流為 `PubSubClient`，但其 QoS 支援有限。若 Agent 2 也使用 MQTT，須統一 broker 設定。

2. **GPIO 33 霍爾感應器移入 Agent 1**：目前 `outdoor_main.cpp` 中 GPIO 33 為 `PIN_LED`（單色 LED）。改為 RGB 後，LED R/G/B 需分別接 GPIO 25/26/27，PIN_HALL 接 GPIO 33，確認接線無衝突。

3. **RGB LED 共陰/共陽**：`LedController` 需根據實際接線決定 HIGH/LOW 對應亮/滅。

4. **SecurityStateMachine 狀態複雜度**：Yellow Alert 等待 Agent 2 alarm_decision 時，MQTT 回應可能延遲，需設定 timeout（建議 10–30 s）後自行決策。

5. **LogManager 記憶體**：50 筆 × 3 類 log，每筆約 100–150 bytes，約 15–22 KB heap。ESP32 heap 通常 ~200 KB，可行但需監控 `ESP.getFreeHeap()`。
