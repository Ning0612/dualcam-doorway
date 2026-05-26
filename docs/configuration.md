# 設定說明

## 目錄

- [設定方式概覽](#設定方式概覽)
- [Config Portal（WiFi 首次設定）](#config-portalwifi-首次設定)
- [NVS 金鑰總覽](#nvs-金鑰總覽)
- [WebUI 設定頁面](#webui-設定頁面)
- [Serial 即時校準](#serial-即時校準)
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
| Dashboard 密碼 | WebUI `/settings` | 修改登入密碼 |
| 霍爾感應器閾值 | Serial `H` 鍵 或 WebUI `/settings` | 門磁感應器現場校準 |
| 人臉辨識閾值 | WebUI `/settings`（預留）| similarity threshold |
| FaceVoter 參數 | `include/config.h` 重新編譯 | 投票視窗調整 |
| 計時常數 | `include/config.h` 重新編譯 | 需燒錄新韌體 |

---

## Config Portal（WiFi 首次設定）

### 觸發條件

以下情況下開機會進入 Config Portal：

1. NVS 中無 `wifi_ssid` / `wifi_pw`（全新裝置）
2. WiFi 連線在 15 秒（`WIFI_CONNECT_TIMEOUT_MS`）內失敗
3. 透過 Serial 輸入 `W` 清除憑證後重啟

### 連線步驟

1. 裝置開啟 WiFi AP：SSID = `Agent1-Setup`，密碼 = `dualcam99`
2. 以手機/電腦連線至 `Agent1-Setup`
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
| `dashboard_pw_hash` | SettingsStore | String | SHA-256(salt+pw) hex | 空（未設定，首次登入強制設定） |
| `discord_url` | SettingsStore | String | Discord Webhook URL | 空 |
| `hall_threshold` | SettingsStore | UInt16 | 霍爾感應器 ADC 閾值 | 2048 |
| `mqtt_broker` | ConfigManager | String | MQTT Broker IP/hostname | 空 |
| `mqtt_port` | ConfigManager | UInt16 | MQTT Broker port | 1883 |
| `face_feat` | FaceRecognizer | Blob | 人臉特徵向量（7×32×4 bytes） | 空 |
| `face_cnt` | FaceRecognizer | UInt8 | 已註冊人臉數量 | 0 |
| `face_names` | FaceRecognizer | Blob | 使用者名稱陣列 | 空 |

---

## WebUI 設定頁面

### WiFi 設定（`/settings/wifi`）

- **WiFi Scan**：掃描附近 AP，點擊選擇 SSID
- **手動輸入**：若 AP 隱藏或掃描未列出
- **送出後**：寫入 NVS → 裝置重啟 → 連線新 WiFi

> 若新 WiFi 設定錯誤，裝置連線失敗後退回 AP 模式。

### Discord 設定（`/settings/discord`）

URL 格式驗證：
- 必須以 `https://discord.com/api/webhooks/` 或 `https://discordapp.com/api/webhooks/` 開頭
- 長度 ≤ 256 字元
- 清空送出 → 清除 Discord 通知

### 系統設定（`/settings/system`）

| 設定項目 | 說明 | 限制 |
|---------|------|------|
| MQTT Broker | IP 或 hostname | 最多 64 字元；空值 = 停用 MQTT |
| MQTT Port | 埠號 | 1–65535；預設 1883 |
| 霍爾閾值 | ADC 12-bit 閾值 | 0–4095（需考慮 Hysteresis ±150） |
| Face Similarity | Cosine similarity 閾值 | 0.80–1.00；建議 0.92 |
| Dashboard 密碼 | 新密碼（需輸入兩次） | 8–64 字元 |

---

## Serial 即時校準

無需重新燒錄即可調整霍爾感應器閾值：

### 校準步驟

```
1. 確保門關閉，Serial 輸入 'h'
   → 輸出: [Agent1] Hall raw=850  threshold=2048  hysteresis=±150  door=CLOSED

2. 開啟門，再次輸入 'h'
   → 輸出: [Agent1] Hall raw=3100  threshold=2048  hysteresis=±150  door=OPEN

3. 半開門（閾值中間點），輸入 'H'
   → 系統將當前 raw 值儲存為新閾值

# 驗證
4. 輸入 'h' 確認新閾值已套用
```

### 閾值計算建議

```
關閉時 ADC ≈ 850
開啟時 ADC ≈ 3100
建議閾值 = (850 + 3100) / 2 = 1975

Hysteresis = ±150:
  → OPEN  觸發點: 1975 + 150 = 2125（ADC 超過此值 → DOOR_OPEN）
  → CLOSE 觸發點: 1975 - 150 = 1825（ADC 低於此值 → DOOR_CLOSED）
```

---

## 程式碼常數（config.h）

以下常數需修改 `include/config.h` 後重新編譯燒錄：

### 網路 / mDNS / HTTP

```cpp
#define MDNS_AGENT1        "agent1"    // mDNS 主機名稱 → agent1.local
#define MDNS_AGENT2        "agent2"    // Agent 2 mDNS → agent2.local
#define HTTP_PORT          80          // Dashboard HTTP port
#define PEER_QUERY_INTERVAL_MS  5000UL // 舊版 HTTP fallback 輪詢間隔（保留）
```

### WiFi / 連線

```cpp
#define WIFI_CONNECT_TIMEOUT_MS  15000UL   // STA 連線嘗試逾時（ms）
#define PORTAL_TIMEOUT_MS       300000UL   // AP 模式無操作逾時（5分鐘）
#define WIFI_LOST_TIMEOUT_MS    300000UL   // loop() 中持續斷線 → restart（5分鐘）
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
#define HALL_DEFAULT_THRESHOLD    2048     // 預設 ADC 閾值
#define HALL_HYSTERESIS            150     // 死區寬度（±各側）
#define HALL_SAMPLE_INTERVAL_MS     50UL   // ADC 採樣頻率
#define DOOR_DEBOUNCE_MS           200UL   // 狀態穩定確認時間
```

### Camera

```cpp
#define CAMERA_DETECT_INTERVAL_MS   500UL  // 人臉偵測頻率（兩次/秒）
#define CAMERA_ENROLL_TIMEOUT_MS  10000UL  // enroll 等待逾時
#define FACE_RECENT_MS           10000UL   // 視為「當前有臉」的時間窗口
```

### 人臉辨識（也可透過 WebUI 調整）

```cpp
#define FACE_SIMILARITY_THRESHOLD  0.92f   // KNOWN 判定閾值
#define FACE_TEXTURE_MIN_STDDEV   20.0f    // 最低紋理分數
```

### FaceVoter（也可透過 WebUI 調整）

```cpp
#define FACE_VOTE_WINDOW_MS          10000UL  // UNKNOWN_CONFIRMED 最短持續時間
#define FACE_VOTE_IDLE_MS             5000UL  // 無臉 idle reset 時間
#define FACE_VOTE_KNOWN_MIN               3   // KNOWN_CONFIRMED 最少 hit 數
#define FACE_VOTE_KNOWN_WINDOW_MS     8000UL  // KNOWN hit 累積時間窗口
#define FACE_VOTE_UNKNOWN_MIN_HITS       10   // UNKNOWN_CONFIRMED 最少 frame 數
```

### Discord

```cpp
#define DISCORD_RATE_LIMIT_MS    30000UL    // 同事件最短間隔（30秒）
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
- 霍爾感應器閾值
- Dashboard 密碼（記錄明文，重新設定）
- 人臉特徵無法匯出（需重新註冊）

> 燒錄時若勾選「Erase Flash」，NVS 將被清除。建議只燒錄 `firmware.bin`，不 erase。

---

## 重設為出廠預設值

### 清除 WiFi 憑證（重進 Config Portal）

```
Serial 輸入: W
→ [Agent1] Clearing WiFi credentials and restarting...
→ 裝置重啟進入 AP 模式 "Agent1-Setup"
```

### 清除所有人臉資料

```
Serial 輸入: r
→ 清除 NVS face_feat, face_cnt, face_names
→ FaceVoter 重置

# 或透過 WebUI
POST /api/face/clear（需 session）
```

### 完整 NVS 清除（完全重設）

需透過 PlatformIO 或 esptool.py 執行 `Erase Flash` 操作，再重新燒錄。

```powershell
# 使用 PlatformIO CLI Erase Flash
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e agent1 -t erase

# 重新燒錄
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e agent1 -t upload
```

> **注意**：`Erase Flash` 會清除所有 NVS 資料，包含 WiFi 憑證、人臉資料、所有設定。
