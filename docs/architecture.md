# 軟體架構說明

## 目錄

- [設計原則](#設計原則)
- [模組依賴圖](#模組依賴圖)
- [主迴圈資料流](#主迴圈資料流)
- [模組職責詳述](#模組職責詳述)
- [狀態機設計](#狀態機設計)
- [FaceVoter 時間窗投票](#facevoter-時間窗投票)
- [Callback 架構](#callback-架構)
- [FreeRTOS 使用](#freertos-使用)
- [記憶體規劃](#記憶體規劃)

---

## 設計原則

1. **單一主迴圈**：`loop()` 統一驅動所有模組的 `tick()`，避免複雜的 task 同步問題
2. **Callback 解耦**：模組間透過函式指標 callback 通訊，不直接相互引用
3. **離線優先**：Agent 2 離線時 Agent 1 完整獨立運作（ALERT_RED 模式）
4. **投票防誤觸**：所有人臉辨識結果必須通過 FaceVoter 時間窗投票才能觸發事件
5. **非阻塞設計**：Discord 通知、Camera 初始化、MJPEG stream 皆以 FreeRTOS task 執行

---

## 模組依賴圖

```
main.cpp
  ├── ConfigPortal        (WiFi 首次設定)
  ├── SettingsStore       (密碼、Discord URL、霍爾閾值)
  ├── ConfigManager       (MQTT 設定)
  ├── SessionAuth         ──► WebServer
  ├── DashboardServer     ──► WebServer, SecurityStateMachine, FaceVoter, LogManager
  │
  ├── DoorSensor          ──► onDoorChange callback ──► SecurityStateMachine
  ├── LedController       (AlertLevel → WS2812B)
  ├── BuzzerController    (alarm trigger/cancel)
  │
  ├── SecurityStateMachine
  │     ├── onAlert callback
  │     ├── onDoorEvent callback
  │     ├── onKnownConfirmed callback
  │     └── onAlarmCancelled callback
  │
  ├── AgentComm (MQTT)
  │     ├── onPresence callback ──► SecurityStateMachine
  │     ├── onAlarmDecision callback ──► SecurityStateMachine
  │     └── onConnectionChange callback ──► SecurityStateMachine
  │
  ├── DiscordNotifier     (HTTPS webhook)
  ├── LogManager          (Face/Door/Alert ring buffer)
  │
  ├── CameraAgent         ──► FaceRecognizer (內部呼叫)
  │     └── MJPEG stream task (port 81)
  ├── FaceRecognizer      (人臉特徵、NVS 儲存)
  └── FaceVoter           ──► VoteResult ──► SecurityStateMachine
```

---

## 主迴圈資料流

```
loop() 每次迭代：

1. server.handleClient()        HTTP 請求處理
2. CameraAgent::handleStreamClients()  (no-op，已由 task 管理)
3. DoorSensor::tick()           ADC 採樣、去彈跳 → onDoorChange()
4. LedController::tick()        閃爍效果更新
5. AgentComm::tick()            MQTT loop 與重連

6. CameraAgent::tick()          每 500ms 執行一次人臉偵測
   └── FaceRecognizer::recognize()  → _lastResult, _lastRunMs

7. FaceVoter::update(lastRawResult, lastRawResultMs, now)
   ├── KNOWN_CONFIRMED  → faceVoter.setConfirmedName(FaceRecognizer::getLastMatchName())
   │                    → sm.onVoteResult(KNOWN_CONFIRMED, name, sim)
   │                       └── SecurityStateMachine 觸發 onKnownConfirmed callback
   └── UNKNOWN_CONFIRMED → sm.onVoteResult(UNKNOWN_CONFIRMED)
                            └── SecurityStateMachine 觸發 onAlert callback
                            注意：持續存在的未知訪客每 FACE_VOTE_WINDOW_MS（10s）重新觸發一次

8. 每 30s：AgentComm::publishStatus(alertLevel, uptime)

9. sm.tick()                    決策逾時處理（Yellow alert decision timeout）
10. handleWifiLoss()            WiFi 斷線監控
11. handleSerialInput()         Serial 指令處理
```

---

## 模組職責詳述

### ConfigPortal

- 開機時讀取 NVS `wifi_ssid` / `wifi_pw`
- 若存在：`WiFi.begin()` 嘗試連線（超時 15s）
- 若失敗或不存在：進入 AP 模式 `Agent1-Setup`，密碼 `dualcam99`
- 提供 HTML 設定頁面（含 WiFi Scan）
- 接收 POST `/save` → 寫入 NVS → `ESP.restart()`
- 5 分鐘無操作 → `ESP.restart()`

### SettingsStore

管理 NVS namespace `agent_cfg` 中的使用者設定：

| NVS Key | 型別 | 說明 |
|---------|------|------|
| `dashboard_pw_hash` | String | salted SHA-256 hex，dashboard 登入密碼 |
| `discord_url` | String | Discord Webhook URL |
| `hall_threshold` | UInt16 | 霍爾感應器 ADC 閾值 |

### ConfigManager

管理 MQTT 連線設定：

| NVS Key | 型別 | 說明 |
|---------|------|------|
| `mqtt_broker` | String | MQTT Broker IP/hostname |
| `mqtt_port` | UInt16 | 預設 1883 |

### DoorSensor

- 每 50ms 採樣 8 次 ADC → 平均值（降噪）
- 以 Hysteresis（±150）防止閾值邊界抖動
- 需要穩定 200ms（`DOOR_DEBOUNCE_MS`）才確認狀態切換
- 確認後透過 callback `onDoorChange(DoorState)` 通知

### LedController

- 使用 Adafruit NeoPixel 驅動單顆 WS2812B
- `setLevel(AlertLevel)` 設定基底顏色
- `setBlinking(true)` 啟用 250ms 閃爍
- `tick()` 管理閃爍時序（millis-based，不 blocking）

### BuzzerController

- `trigger()` 啟動蜂鳴器（持續鳴響）
- `cancel()` 停止蜂鳴器

### SecurityStateMachine

核心決策模組，詳見 [狀態機設計](#狀態機設計)。

### AgentComm

- MQTT Broker 未設定時（空字串），`begin()` 為 no-op，`isConnected()` 回傳 false
- MQTT 斷線時每 5s 自動重連，重連後立即重新 subscribe
- 接收 MQTT `home/home_state/presence` → `onPresence(occupied, score)` callback
- 接收 MQTT `home/home_state/alarm_decision` → `onAlarmDecision(decision)` callback
- MQTT 連線狀態變更 → `onConnectionChange(connected)` callback

### DiscordNotifier

- TLS 連線（開發環境用 `setInsecure()`，生產環境需設定 CA 憑證）
- URL 白名單驗證（只允許 `https://discord.com/api/webhooks/` 開頭）
- 每種 AlertEvent 獨立 rate limiting（同事件最少 30s 間隔）
- 連線失敗後進入 5 分鐘 cooldown
- `notifyBoot()` 不影響 rate limit，確保開機通知不阻礙後續警報

### CameraAgent

- `begin()`：初始化 OV2640 Camera（QQVGA, YUV422, PSRAM 模式）
- `tick()`：每 500ms 執行一次，呼叫 `FaceRecognizer::recognize()`，儲存結果
- `scheduleEnroll(name)`：排程下一次偵測為人臉註冊，10s 逾時自動取消
- MJPEG stream：獨立 FreeRTOS task，port 81，每個連線一個 frame

### FaceRecognizer

詳見 [人臉辨識文件](face-recognition.md)。

### FaceVoter

詳見 [FaceVoter 時間窗投票](#facevoter-時間窗投票)。

### LogManager

- 三個獨立環形緩衝區（Face Log, Door Log, Alert Log），各最多 50 筆
- 超出時覆蓋最舊的記錄
- NTP 同步時用 `time()` 取得 ISO 8601 timestamp；未同步時用 millis() 相對時間
- `getFaceLogJson()` / `getDoorLogJson()` / `getAlertLogJson()` 回傳 JSON 陣列

### SessionAuth

- Session token：16-byte random hex，in-memory（重啟後失效）
- Cookie：`sid=<token>; HttpOnly; Path=/; SameSite=Lax`
- TTL：30 分鐘，每次授權請求重置
- CSRF token：per-boot random hex，所有 state-changing POST 驗證
- Brute-force：5 次失敗後鎖定 60 秒

### DashboardServer

- 所有 HTML 儲存於 PROGMEM `const char[]`（無 LittleFS）
- 每頁 HTML < 6KB（含 inline CSS + JS）
- `/api/status` 使用 `StaticJsonDocument<512>`
- 3 秒 AJAX 輪詢更新 Dashboard

---

## 狀態機設計

`SecurityStateMachine` 根據三個輸入計算 `AlertLevel`：

### AlertLevel 計算規則

```
AlertLevel _recalcAlertLevel():

  if (!_agent2Online || !_occupied):
      return ALERT_RED    ← 獨立警戒模式（預設）

  if (_occupied && _agent2Online):
      if (_knownConfirmed && !expired):
          return ALERT_GREEN  ← 已知使用者，短暫綠燈
      return ALERT_YELLOW     ← 協調模式
```

| 條件 | AlertLevel |
|------|------------|
| Agent 2 離線 | `ALERT_RED` |
| presence = UNOCCUPIED | `ALERT_RED` |
| presence = OCCUPIED | `ALERT_YELLOW` |
| KNOWN_CONFIRMED（15s 內） | `ALERT_GREEN` |

### 未知訪客處理流程

```
onVoteResult(UNKNOWN_CONFIRMED)
  ├─ AlertLevel = RED:
  │    LedController::setBlinking(true)
  │    BuzzerController::trigger()
  │    DiscordNotifier::notify(UNKNOWN_VISITOR)
  │    LogManager::logAlert(RED, "UNKNOWN_CONFIRMED", TRIGGER_ALARM)
  │
  └─ AlertLevel = YELLOW:
       AgentComm::publishAlert(YELLOW, "UNKNOWN_CONFIRMED")
       _waitingForDecision = true
       _decisionStartMs = now
       LogManager::logAlert(YELLOW, "UNKNOWN_CONFIRMED", NO_ACTION)
       
       30s 後無 AlarmDecision → 自動觸發 TRIGGER_ALARM
```

### Yellow Alert 決策逾時

```
sm.tick():
  if (_waitingForDecision && elapsed >= ALARM_DECISION_TIMEOUT_MS):
      _triggerAlarm()   ← 視為 Agent 2 未回應 → 預設升為 RED alert
```

### 警報取消流程

```
onAlarmDecision(CANCEL_ALARM):
  if (_alarmActive):
      BuzzerController::cancel()
      LedController::setBlinking(false)
      LedController::setLevel(getAlertLevel())
      _onAlarmCancelled()
```

---

## FaceVoter 時間窗投票

FaceVoter 防止單一 frame 誤觸發，所有人臉辨識結果必須先通過投票。

### KNOWN_CONFIRMED 規則

```
需要：FACE_VOTE_KNOWN_MIN (3) 次 KNOWN hit
在：  FACE_VOTE_KNOWN_WINDOW_MS (8000ms) 窗口內

timeline:
  0ms    - 第1次 KNOWN → _knownCount=1, _firstKnownMs=0ms
  2000ms - 第2次 KNOWN → _knownCount=2
  5000ms - 第3次 KNOWN → _knownCount=3 → KNOWN_CONFIRMED ✓
  
  確認後，同一連續存在不重複觸發（_knownConfirmed=true）
  直到 5s 無臉（Idle Reset）才重置
```

### UNKNOWN_CONFIRMED 規則

```
需要：FACE_VOTE_UNKNOWN_MIN_HITS (10) 次 UNKNOWN frame
且：  已持續 FACE_VOTE_WINDOW_MS (10000ms)

timeline:
  0ms    - 第1次 UNKNOWN → _unknownStartMs=0ms
  ...持續偵測到 UNKNOWN...
  10s+   - 且 hits >= 10 → UNKNOWN_CONFIRMED ✓
  
  確認後立即重置計時器，持續存在的訪客每 10s 可重新觸發
  
  重要：偶發的 KNOWN raw result 不會重置 unknown timer
  （只有 KNOWN_CONFIRMED 才清除 unknown 狀態）
```

### Idle Reset

```
5s（FACE_VOTE_IDLE_MS）無任何臉部偵測：
  _knownCount = 0
  _unknownHits = 0
  _unknownStartMs = 0
  _knownConfirmed = false
  _confirmedName = ""
  → 回到初始狀態
```

---

## Callback 架構

```cpp
// SecurityStateMachine callbacks
sm.setOnAlert(onAlert);                   // 未知訪客警報觸發
sm.setOnDoorEvent(onDoorEvent);           // 門狀態確認改變
sm.setOnKnownConfirmed(onKnownConfirmed); // 已知使用者確認
sm.setOnAlarmCancelled(onAlarmCancelled); // 警報被 Agent 2 取消

// AgentComm callbacks
AgentComm::setOnPresence(onPresence);              // MQTT presence 訊息
AgentComm::setOnAlarmDecision(onAlarmDecision);    // MQTT alarm decision
AgentComm::setOnConnectionChange(onAgent2Connection); // MQTT 連線狀態

// DoorSensor callback
DoorSensor::setOnChange(onDoorChange);    // 門狀態確認改變

// FaceRecognizer callback
FaceRecognizer::setOnClearCallback([]{ faceVoter.reset(); });
```

---

## FreeRTOS 使用

| Task 名稱 | Stack | 優先權 | 用途 |
|-----------|-------|--------|------|
| `boot_notify` | 8192 B | 1 | 開機 Discord 通知（5s 延遲後發送） |
| `cam_init` | 8192 B | 1 | Camera 非同步初始化 |
| MJPEG stream task | 4096 B | 5 | MJPEG frame 推送（port 81） |

> `loop()` 在 Arduino 框架的 `app_main` task 中執行（core 1），stack ≈ 8KB。

---

## 記憶體規劃

| 資源 | 大小 | 說明 |
|------|------|------|
| PSRAM | ~4MB | YUV422 Camera frame（QQVGA = 160×120×2 = 38.4KB/frame） |
| DRAM Heap | ~200KB | Arduino heap（FreeRTOS 動態配置） |
| PROGMEM | ~30KB | HTML templates（DashboardServer） |
| NVS | < 4KB | 設定資料（wifi_ssid, discord_url, face_feat 等） |
| Face Bank | 7 × 32 × 4 = 896 bytes | 7 位使用者特徵向量（float32） |
| Log Buffers | 50 × 3 × ~100B ≈ 15KB | Face/Door/Alert ring buffer |
| `StaticJsonDocument<512>` | 512 bytes | `/api/status` JSON |
