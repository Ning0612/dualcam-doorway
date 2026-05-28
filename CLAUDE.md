# CLAUDE.md

FaceGuard：ESP32 NMK99 + OV2640 智慧門口警戒系統。可離線獨立運作；MQTT 選配 Agent 2（室內 agent）協作。

> **⚠️ 從舊韌體升級注意**：此版本將密碼 salt 從 `dualcam_s2024` 改為 `faceguard_s2024`，AP 密碼從 `dualcam99` 改為 `faceguard99`。已部署且設定過 Dashboard 密碼的裝置升級後需執行 NVS 完整清除（`pio run -e faceguard -t erase` 後重燒）並重新設定所有參數。

---

## Build & Flash

`pio` 不在 PATH，Windows 需完整路徑：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e faceguard
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e faceguard -t upload --upload-port COMX
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor --port COMX
```

唯一環境 `faceguard`。**禁止使用 `board = esp32cam`。**

`lib/` 無法自動看到 `include/` — `-I include` 旗標必須存在。

---

## GPIO 約束

**禁用（Camera / boot-strap）：** 0, 2, 5, 6–11, 12, 18–19, 21–23, 25–27, 34–36, 39  
**可用：** 13, 32, 33

```
PIN_LED_DATA = 32  // WS2812B（NMK99 上 CAM_PWDN 未接線，pin_pwdn = -1）
PIN_BUZZER   = 13  // LEDC PWM Channel 7
PIN_HALL     = 33  // ADC1_CH5，WiFi 啟動後仍可用
```

GPIO 34/35/36/39 input-only，無內部上拉。WiFi 重設用 Serial `W`，**不用 GPIO 0**。

---

## 核心狀態 enum（states.h）

```cpp
enum class DoorState   { DOOR_CLOSED, DOOR_OPEN };
enum class VoteResult  { NONE, KNOWN_CONFIRMED, UNKNOWN_CONFIRMED };
enum class AlertLevel  { ALERT_GREEN, ALERT_YELLOW, ALERT_RED };
enum class AlarmDecision { NO_ACTION, TRIGGER_ALARM, CANCEL_ALARM };
enum class AlertEvent  { UNKNOWN_VISITOR = 0, USER_RETURNED = 1, BOOT = 2 };
```

---

## 人臉辨識約束（FaceRecognizer）

演算法為 **HOG-lite**（64-dim，4×4 cells × 4 bins）。詳細算法見 `docs/face-recognition.md`。

**必須遵守：**
- 只處理 YUV422 Y channel；QVGA 320×240；中央 60% ROI
- 梯度用**中央差分**（`dx = Y(x+1)-Y(x-1)`），不用 Sobel
- Active-cell gate 在 L2 normalize **之前**：`activeCells < 4 → NO_FACE`（active cell 定義：`cellEnergy/cellPix > 0.5f`）
- Texture check：`totalEnergy/totalPix < 12.0 → NO_FACE`
- 每人最多 5 個模板；同名 enroll = 追加模板（非覆蓋）
- `similarity ≥ 0.90` 且 margin `≥ 0.03` → FACE_KNOWN
- NVS keys：`face_feat`（blob, max 8960 B）、`face_cnt`、`face_names`、`face_tcnt`

---

## FaceVoter 規則

**所有辨識結果必須過 FaceVoter，禁止單 frame 直接觸發事件。**

| 結果 | 條件 |
|------|------|
| KNOWN_CONFIRMED | ≥ 3 hits 在 8 s 內；同臉不重複觸發直到 idle reset |
| UNKNOWN_CONFIRMED | 持續 UNKNOWN ≥ 10 s 且 ≥ 10 hits；偶發 KNOWN 不重置 unknown timer |
| Idle reset | 5 s 無臉 → 清除所有 counters/timers/confirmedName |

---

## SecurityStateMachine 行為

### AlertLevel 計算

| 條件 | AlertLevel |
|------|-----------|
| Agent 2 離線 OR presence = UNOCCUPIED | ALERT_RED |
| presence = OCCUPIED | ALERT_YELLOW |
| KNOWN_CONFIRMED（60 s 窗口，無告警） | ALERT_GREEN |

### LED 優先級（main.cpp `updateLed()`）

1. `isAlarmActive()` → 紅色閃爍 250ms
2. ALERT_GREEN → 綠色常亮
3. 任意人臉偵測到 → 白色常亮（fill light）
4. 否則 → 關閉

### 警報行為

- **Red UNKNOWN_CONFIRMED**：LED 閃爍 + 蜂鳴器 + `notifyWithPhoto()`（`!jpegBuf || jpegLen==0` 或 multipart alloc 失敗時，`notifyWithPhoto()` 內部 fallback `notify()`；photo HTTP 失敗時，main.cpp 外層 `!sent` 亦 fallback 純文字 `notify()`）+ Alert Log
- **Yellow UNKNOWN_CONFIRMED**：MQTT publish alert → 等 Agent 2 `alarm_decision`（逾時 30 s → TRIGGER_ALARM）
- `_cancelAlarm()` = 完整取消（同時觸發 buzzer silence + alarm cancelled callback）
- `_silenceBuzzer()` ≠ `_cancelAlarm()`，兩者是不同操作

### 警報取消條件（任一）

蜂鳴器持續時間到期 / 門關閉 / KNOWN_CONFIRMED 收到

### 邊界行為

- `onFaceKnownRaw(name)`：每 loop() raw KNOWN 時呼叫，維護 `_lastSeenKnownName`；供門開時使用者歸因
- `onAgent2Online(false)`：若在 `_waitingForDecision` 中，立即 TRIGGER_ALARM
- `onAlarmDecision(TRIGGER_ALARM)`：只在 `_waitingForDecision` 下生效，防 retained MQTT 誤觸；`CANCEL_ALARM` 無論 `_waitingForDecision` 狀態均清除等待並呼叫 `_cancelAlarm()`（alarm 未啟動時為 no-op）
- `_returnFired` 旗標防同一綠燈窗口內重複觸發「已知使用者回家」

---

## 不可違反規則

- 禁止單 frame 直接觸發事件（必過 FaceVoter）
- 禁止略過 texture validation 或 active-cell gate
- 禁止 WebUI 參與核心決策邏輯
- 禁止依賴 Agent 2 才能警戒
- 禁止因 WiFi / Discord / NTP / MQTT 失敗停止本機功能
- 禁止 ESP32 主流程加入大型 CNN
- SSM 禁止直接操作硬體或網路，所有副作用透過 callback

---

## 模組職責

| 模組 | 職責 | 禁止 |
|------|------|------|
| FaceRecognizer | HOG-lite 特徵萃取、cosine similarity、NVS 人臉管理 | 警報決策 |
| FaceVoter | temporal voting、confirmed 判定、idle reset | 影像特徵萃取 |
| CameraAgent | camera init、MJPEG stream、enroll 排程 | 辨識決策 |
| DoorSensor | 霍爾 ADC、去彈跳、DoorState 輸出 | 警戒決策 |
| SecurityStateMachine | AlertLevel 計算、alarm callback | 影像處理、硬體直接控制 |
| AgentComm | MQTT publish/subscribe | 決策邏輯 |
| DiscordNotifier | HTTPS webhook、TLS、rate limit | 阻塞主流程 |
| ConfigPortal | AP mode、WiFi 首次設定 | 其他設定 |
| ConfigManager | MQTT + 蜂鳴器 NVS 讀寫 | WiFi 設定 |
| SettingsStore | dashboard_pw、discord_url、hall_lo/hi NVS 讀寫 | WiFi 設定 |
| DashboardServer | HTTP routes、PROGMEM HTML | 核心決策邏輯 |
| LogManager | RAM ring buffer + SPIFFS NDJSON | 警戒決策 |

---

## NVS Key Map（namespace `"agent_cfg"`）

| Key | Owner | Type | 備註 |
|-----|-------|------|------|
| `wifi_ssid` / `wifi_pw` | ConfigPortal | String | max 32 / 64 chars |
| `dashboard_pw` | SettingsStore | String | salted SHA-256 hex |
| `pw_changed` | SettingsStore | Bool | 首次改密旗標 |
| `discord_url` | SettingsStore | String | max 256 chars |
| `hall_lo` / `hall_hi` | SettingsStore | UInt32 | open zone 邊界；form field 名為 `hall_lower`/`hall_upper` |
| `mqtt_broker` | ConfigManager | String | max 63 chars |
| `mqtt_port` | ConfigManager | UInt16 | default 1883 |
| `buzzer_freq` | ConfigManager | UInt32 | Hz；WebUI 輸入 Hz |
| `buzzer_dur` | ConfigManager | UInt32 | ms；WebUI 輸入秒×1000 |
| `face_feat` | FaceRecognizer | Blob | max 8960 B（7×5×64×4） |
| `face_cnt` | FaceRecognizer | UInt8 | enrolled user 數 |
| `face_names` / `face_tcnt` | FaceRecognizer | Blob | 119 B / MAX_FACES B |

> NVS size mismatch on `_load()` → sets `_n = 0`，呼叫 `_persist()`（非 clearAll callback）

---

## 關鍵計時常數（config.h）

| 常數 | 值 | 說明 |
|------|-----|------|
| `FACE_SIMILARITY_THRESHOLD` | 0.90 | cosine similarity 門檻 |
| `FACE_MARGIN_MIN` | 0.03 | 防誤認分差 |
| `FACE_TEXTURE_MIN_STDDEV` | 12.0 | mean L1 gradient 最低值 |
| `FACE_VOTE_KNOWN_MIN` | 3 | KNOWN_CONFIRMED 最少 hits |
| `FACE_VOTE_KNOWN_WINDOW_MS` | 8000 | KNOWN 累積窗口 |
| `FACE_VOTE_WINDOW_MS` | 10000 | UNKNOWN_CONFIRMED 持續時間 |
| `FACE_VOTE_UNKNOWN_MIN_HITS` | 10 | UNKNOWN_CONFIRMED 最少 hits |
| `FACE_VOTE_IDLE_MS` | 5000 | 無臉 idle reset |
| `HALL_DEFAULT_LOWER/UPPER` | 1000 / 3000 | open zone 預設邊界 |
| `HALL_HYSTERESIS` | 150 | 死區半寬 |
| `BUZZER_DURATION_MS` | 60000 | 警報後呼叫 `_cancelAlarm()` |
| `AGENT2_OFFLINE_TIMEOUT_MS` | 15000 | 無 presence → 離線 |
| `ALARM_DECISION_TIMEOUT_MS` | 30000 | Yellow alert 等待 Agent 2（SecurityStateMachine.h） |
| `KNOWN_GREEN_DURATION_MS` | 60000 | KNOWN_CONFIRMED 保持 GREEN（SecurityStateMachine.h） |
| `DISCORD_RATE_LIMIT_MS` | 30000 | 同 AlertEvent 最少間隔 |
| `DISCORD_FAIL_COOLDOWN_MS` | 300000 | 連線失敗後封鎖 |

---

## 錯誤處理

| 錯誤 | 行為 |
|------|------|
| WiFi 失敗 | 不停本機警戒；重連後恢復 MQTT / Discord |
| WiFi 持續斷線 5 min | `ESP.restart()` → Config Portal |
| NTP 失敗 | 繼續運作；log 用相對時間；略過 SPIFFS 寫入 |
| Camera 失敗 | face state → NO_FACE；不 crash |
| Agent 2 離線 | ALERT_RED；FaceGuard 獨立警戒 |
| MQTT 斷線 | 背景重連；重連後重新 subscribe |
| Discord 失敗 | 不阻塞；5 min cooldown；`notifyBoot()` 不受影響 |

---

## Session Auth

- Username: `admin`（hardcoded）；Password: salted SHA-256 存 NVS `dashboard_pw`
- Session token: 16-byte random hex，in-memory，TTL 30 min（每次請求重置）
- Cookie: `sid=...; HttpOnly; Path=/; SameSite=Lax`
- CSRF: 所有 state-changing POST 必驗；per-boot random hex token
- Brute-force: 5 次失敗 → 60 s lockout

---

## WebUI 關鍵約束

- HTML 在 PROGMEM `const char[]`，無 LittleFS、無外部 CDN
- WiFi 設定**僅限** Config Portal（AP 模式），WebUI 不處理
- `session + 改密` = 需 session 且 `pw_changed == true`，否則 redirect `/password/change`
- 所有 state-changing POST 需 CSRF token
- 完整路由表見 `docs/webui.md`

---

## MQTT Topics

```
Publish:   home/security/door | face | alert | status
Subscribe: home/home_state/presence | alarm_decision
```

Agent 2 離線（15 s 無 presence）→ ALERT_RED。完整 payload 格式見 `docs/mqtt.md`。

---

## Discord 規則

- URL whitelist: `https://discord.com/api/webhooks/` 或 `https://discordapp.com/api/webhooks/`
- TLS: 需定義 `DISCORD_TLS_INSECURE` 或 `DISCORD_ROOT_CA_CERT`，否則編譯失敗
- fallback：`notifyWithPhoto()` 內部僅在 `!jpegBuf || jpegLen==0` 或 multipart alloc 失敗時 fallback `notify()`；main.cpp 外層在 `!sent`（含 HTTP 失敗）亦 fallback 純文字 `notify()`
- Log: 只印 webhook URL 最後 8 字元

---

## Logging Convention

```
[FaceGuard] boot
[FaceGuard] WiFi connected. IP: 192.168.x.x
[FaceGuard] face KNOWN_CONFIRMED: Alice (sim=0.95)
[FaceGuard] face UNKNOWN_CONFIRMED
[FaceGuard] door OPEN
[FaceGuard] alert RED: buzzer + discord
[FaceGuard] alarm_decision: CANCEL_ALARM (from Agent2)
[FaceGuard] discord sent (last 8: xxxxxxxx)
[FaceGuard] WARNING: Agent2 offline — default ALERT_RED
```
