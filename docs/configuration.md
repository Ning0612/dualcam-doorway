# 設定說明

## 目錄

- [設定方式概覽](#設定方式概覽)
- [Config Portal（WiFi 首次設定）](#config-portalwifi-首次設定)
- [NVS 金鑰總覽](#nvs-金鑰總覽)
- [WebUI 設定頁面](#webui-設定頁面)
- [程式碼常數（config.h）](#程式碼常數configh)
- [NVS 資料備份與還原](#nvs-資料備份與還原)
- [重設為出廠預設值](#重設為出廠預設值)

---

## 設定方式概覽

| 設定類型 | 方式 | 說明 |
|---------|------|------|
| WiFi 憑證 | Config Portal（AP 模式） | 首次設定或憑證遺失時 |
| Discord Webhook | WebUI `/settings` | 設定通知 URL |
| MQTT Broker | WebUI `/settings` | Agent 2 通訊設定 |
| 蜂鳴器頻率/持續時間 | WebUI `/settings` | 警報聲音調整 |
| Dashboard 密碼 | WebUI `/settings` | 修改登入密碼 |
| 霍爾感應器開閉區間 | WebUI `/settings` | 門磁感應器 lower/upper bounds |
| FaceVoter / 計時常數 | `include/config.h` 重新編譯 | 需燒錄新韌體 |
| 人臉辨識閾值 | `include/config.h` 重新編譯 | similarity / margin / texture |

---

## Config Portal（WiFi 首次設定）

### 觸發條件

以下情況下開機會進入 Config Portal：

1. NVS 中無 `wifi_ssid` / `wifi_pw`（全新裝置）
2. WiFi 連線在 15 秒（`WIFI_CONNECT_TIMEOUT_MS`）內失敗
3. 透過 Serial 輸入 `W` 清除憑證後重啟

### 連線步驟

1. 裝置開啟 WiFi AP：SSID = `FaceGuard-Setup`，密碼 = `faceguard99`
2. 以手機/電腦連線至 `FaceGuard-Setup`
3. 瀏覽器訪問 `http://192.168.4.1`
4. 頁面顯示 WiFi Scan 結果，選擇 SSID 或手動輸入
5. 輸入 WiFi 密碼，點擊「儲存」→ 裝置重啟並連線至指定 WiFi
6. 連線成功後，透過 WebUI `/settings` 設定 Discord Webhook URL 和其他參數

### AP 逾時

若 5 分鐘（`PORTAL_TIMEOUT_MS`）內未接收到 POST /save，裝置自動重啟。

### 設定欄位

| 欄位 | NVS Key | 限制 | 必填 |
|------|---------|------|------|
| WiFi SSID | `wifi_ssid` | 最多 32 字元 | 是 |
| WiFi 密碼 | `wifi_pw` | 最多 64 字元 | 是（開放網路填空） |

> Config Portal **只**儲存 WiFi 憑證。Discord URL、MQTT 等設定請在 WiFi 連線成功後透過 WebUI `/settings` 設定。

---

## NVS 金鑰總覽

所有金鑰均在 namespace `"agent_cfg"` 下儲存：

| NVS Key | 管理模組 | 型別 | 說明 | 預設值 |
|---------|---------|------|------|-------|
| `wifi_ssid` | ConfigPortal | String | WiFi SSID | 空（未設定） |
| `wifi_pw` | ConfigPortal | String | WiFi 密碼 | 空（未設定） |
| `dashboard_pw` | SettingsStore | String | salted SHA-256 hex 密碼 | 空（首次登入強制設定） |
| `pw_changed` | SettingsStore | Bool | 是否已修改過預設密碼 | false |
| `discord_url` | SettingsStore | String | Discord Webhook URL | 空 |
| `hall_lo` | SettingsStore | UInt32 | 霍爾感應器 open zone 下界 | 1000 |
| `hall_hi` | SettingsStore | UInt32 | 霍爾感應器 open zone 上界 | 3000 |
| `mqtt_broker` | ConfigManager | String | MQTT Broker IP/hostname | 空 |
| `mqtt_port` | ConfigManager | UInt16 | MQTT Broker port | 1883 |
| `buzzer_freq` | ConfigManager | UInt32 | 蜂鳴器頻率 Hz | 2000 |
| `buzzer_dur` | ConfigManager | UInt32 | 蜂鳴器持續時間 ms | 60000 |
| `face_feat` | FaceRecognizer | Blob | 特徵向量（最大 7×5×64×4 = 8960 bytes） | 空 |
| `face_cnt` | FaceRecognizer | UInt8 | 已註冊使用者數量 | 0 |
| `face_names` | FaceRecognizer | Blob | 使用者名稱（最大 7×17 = 119 bytes；實際依 enrolled count） | 空 |
| `face_tcnt` | FaceRecognizer | Blob | 每位使用者模板數（最大 7 bytes；實際依 enrolled count） | 空 |

---

## WebUI 設定頁面

所有設定集中在統一的 `/settings` 頁面（`GET /settings`），需登入且已修改預設密碼。
儲存透過 `POST /settings/save`（含 CSRF token 驗證）。

### 可設定項目

| 設定項目 | NVS Key | 說明 |
|---------|---------|------|
| Discord Webhook URL | `discord_url` | 必須以 `https://discord.com/api/webhooks/` 或 `https://discordapp.com/api/webhooks/` 開頭；清空 = 停用 |
| MQTT Broker | `mqtt_broker` | IP 或 hostname；最多 63 字元；空值 = 停用 MQTT |
| MQTT Port | `mqtt_port` | 1–65535；預設 1883 |
| 霍爾感應器下界 | `hall_lo` | open zone 下界（ADC 0–4095）；預設 1000 |
| 霍爾感應器上界 | `hall_hi` | open zone 上界（ADC 0–4095）；預設 3000 |
| 蜂鳴器頻率 | `buzzer_freq` | 200–8000 Hz；WebUI 輸入 Hz，直接存 NVS |
| 蜂鳴器持續時間 | `buzzer_dur` | WebUI 輸入秒（10–300 s），存 NVS 時轉為毫秒 |
| Dashboard 密碼 | `dashboard_pw` | 新密碼（需輸入兩次確認） |

> WiFi 設定**不**在 WebUI，只能透過 Config Portal（AP 模式）設定。

### 霍爾感應器雙邊界說明

霍爾感應器以 **open zone**（開啟區間 `[hall_lo, hall_hi]`）取代單一閾值，
加上 hysteresis 死區防止邊界震盪：

```
OPEN  觸發（進入 inner band）：
  raw > hall_lo + HYSTERESIS(150) 且 raw < hall_hi - HYSTERESIS(150)

CLOSED 觸發（超出 outer band）：
  raw < hall_lo - HYSTERESIS(150)  或  raw > hall_hi + HYSTERESIS(150)

Dead zone（維持現狀）：
  在 inner/outer band 之間的模糊區間
```

範例（hall_lo=1000, hall_hi=3000, HYSTERESIS=150）：
```
raw < 850          → DOOR_CLOSED（outer band 外側）
850 ≤ raw ≤ 1150   → Dead zone（維持現狀）
1150 < raw < 2850  → DOOR_OPEN（inner band 內側）
2850 ≤ raw ≤ 3150  → Dead zone（維持現狀）
raw > 3150         → DOOR_CLOSED（outer band 外側）
```

校準建議：
1. 記錄門關閉時的 ADC raw 值（`/api/status` 的 `hall_raw`）
2. 記錄門完全開啟時的 ADC raw 值
3. 設定 `hall_lo` ≈ 開門值下緣 + HYSTERESIS×2，`hall_hi` ≈ 開門值上緣 - HYSTERESIS×2
4. 確保門關閉時 ADC 落在 outer band 之外（`< hall_lo - 150` 或 `> hall_hi + 150`）

---

## 程式碼常數（config.h）

以下常數需修改 `include/config.h` 後重新編譯燒錄：

### WiFi / 連線

```cpp
#define WIFI_CONNECT_TIMEOUT_MS  15000UL   // STA 連線嘗試逾時
#define PORTAL_TIMEOUT_MS       300000UL   // AP 模式無操作逾時（5分鐘）
#define WIFI_LOST_TIMEOUT_MS    300000UL   // 持續斷線 → restart（5分鐘）
```

### MQTT

```cpp
#define MQTT_DEFAULT_PORT         1883
#define MQTT_RECONNECT_MS         5000UL   // 重連間隔
#define MQTT_KEEPALIVE_S            60     // Keep-alive 週期
#define AGENT2_OFFLINE_TIMEOUT_MS 15000UL  // 無 presence → Agent 2 離線判定
```

### 霍爾感應器

```cpp
#define HALL_DEFAULT_LOWER        1000     // open zone 下界預設
#define HALL_DEFAULT_UPPER        3000     // open zone 上界預設
#define HALL_HYSTERESIS            150     // 死區半寬（各邊界各側）
#define HALL_SAMPLE_INTERVAL_MS     50UL   // ADC 採樣頻率
#define DOOR_DEBOUNCE_MS           200UL   // 狀態穩定確認時間
```

### Camera

```cpp
#define CAMERA_DETECT_INTERVAL_MS   500UL  // 人臉偵測頻率（兩次/秒）
#define CAMERA_ENROLL_TIMEOUT_MS  10000UL  // enroll 等待逾時
#define FACE_RECENT_MS           10000UL   // 視為「當前有臉」的時間窗口
```

### 人臉辨識

```cpp
#define FACE_SIMILARITY_THRESHOLD  0.90f   // KNOWN 判定 cosine similarity 閾值
#define FACE_MARGIN_MIN            0.03f   // 最高/次高分差（防近似人臉混淆）
#define FACE_TEXTURE_MIN_STDDEV   12.0f    // mean L1 gradient per pixel 最低值
```

### FaceVoter

```cpp
#define FACE_VOTE_WINDOW_MS          10000UL  // UNKNOWN_CONFIRMED 最短持續時間
#define FACE_VOTE_IDLE_MS             5000UL  // 無臉 idle reset 時間
#define FACE_VOTE_KNOWN_MIN               3   // KNOWN_CONFIRMED 最少 hit 數
#define FACE_VOTE_KNOWN_WINDOW_MS     8000UL  // KNOWN hit 累積時間窗口
#define FACE_VOTE_UNKNOWN_MIN_HITS       10   // UNKNOWN_CONFIRMED 最少 frame 數
```

### 蜂鳴器

```cpp
#define BUZZER_DEFAULT_FREQ_HZ    2000U    // 被動壓電式蜂鳴器頻率（Hz）
#define BUZZER_DURATION_MS       60000UL   // 警報蜂鳴器自動取消時間（呼叫 _cancelAlarm）
#define BUZZER_TEST_DURATION_MS    500UL   // 測試嗶聲持續時間
```

### Discord

```cpp
#define DISCORD_RATE_LIMIT_MS    30000UL    // 同 AlertEvent 最短間隔（30秒）
#define DISCORD_TIMEOUT_MS        5000UL    // 連線 + 讀取逾時
#define DISCORD_FAIL_COOLDOWN_MS 300000UL   // 連線失敗後 cooldown（5分鐘）
```

### Session Auth

```cpp
#define DASHBOARD_SESSION_TTL_MS 1800000UL  // Session 30分鐘 TTL
#define LOGIN_LOCKOUT_MS         60000UL    // 暴力破解鎖定時間
#define LOGIN_MAX_FAILS               5     // 最大失敗次數
```

---

## NVS 資料備份與還原

NVS 資料目前無自動備份機制。若需保留設定（例如韌體更新時），建議在更新前記錄：

- MQTT Broker IP 與 Port
- Discord Webhook URL
- 霍爾感應器 hall_lo / hall_hi
- 蜂鳴器頻率 / 持續時間
- Dashboard 密碼（記錄明文，重新設定）
- 人臉特徵無法匯出（需重新註冊）

> 燒錄時若勾選「Erase Flash」，NVS 將被清除。建議只燒錄 `firmware.bin`，不 erase。

---

## 重設為出廠預設值

### 清除 WiFi 憑證（重進 Config Portal）

```
Serial 輸入: W
→ [FaceGuard] Clearing WiFi credentials and restarting...
→ 裝置重啟進入 AP 模式 "FaceGuard-Setup"
```

### 清除所有人臉資料

```
# 透過 WebUI（需 session + CSRF）
POST /api/face/clear
→ face_cnt = 0，face_feat / face_tcnt / face_names 從 NVS 移除
→ _n = 0，所有比對回傳 NO_FACE，直到重新 enroll
```

### 完整 NVS 清除（完全重設）

需透過 PlatformIO 或 esptool.py 執行 `Erase Flash` 操作，再重新燒錄。

```powershell
# 使用 PlatformIO CLI Erase Flash
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e faceguard -t erase

# 重新燒錄
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e faceguard -t upload
```

> **注意**：`Erase Flash` 會清除所有 NVS 資料，包含 WiFi 憑證、人臉資料、所有設定。
