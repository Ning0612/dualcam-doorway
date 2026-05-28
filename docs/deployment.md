# 部署說明

> **⚠️ 從舊韌體（DualCam/agent1）升級的破壞性變更**：
> - Dashboard 密碼 salt 已變更（`dualcam_s2024` → `faceguard_s2024`），已設定過密碼的裝置升級後將**無法登入** WebUI
> - Config Portal AP 密碼已變更：`dualcam99` → `faceguard99`
> - MQTT client ID 已變更：`agent1` → `faceguard`（若 Broker ACL 有限制請更新）
>
> **恢復方式**：執行完整 NVS 清除後重燒 → `pio run -e faceguard -t erase` → 重新設定所有項目

## 目錄

- [部署前準備](#部署前準備)
- [生產環境設定](#生產環境設定)
- [部署步驟](#部署步驟)
- [網路規劃](#網路規劃)
- [安全性強化](#安全性強化)
- [監控與維護](#監控與維護)
- [韌體更新流程](#韌體更新流程)
- [故障排除](#故障排除)
- [錯誤處理行為參考](#錯誤處理行為參考)

---

## 部署前準備

### 硬體確認清單

- [ ] ESP32 NMK99 板子正常供電（5V/2A）
- [ ] OV2640 Camera 排線確實插入，無鬆脫
- [ ] WS2812B LED 接至 GPIO 32，GND 共地
- [ ] 霍爾感應器接至 GPIO 33，外接 10kΩ 上拉電阻（若需要）
- [ ] 蜂鳴器接至 GPIO 13，GND 共地
- [ ] 確認無短路，上電測試 LED 亮紅色（ALERT_RED 開機預設）

### 軟體確認清單

- [ ] `platformio.ini` 中的 `DISCORD_TLS_INSECURE` 已替換為 CA 憑證（生產環境）
- [ ] 韌體已成功編譯（無 error / warning）
- [ ] 目標 COM Port 確認（Windows: `COMx`，Linux: `/dev/ttyUSBx`）

---

## 生產環境設定

### TLS 憑證設定

**開發環境（`DISCORD_TLS_INSECURE`，不驗證 TLS）：**
```ini
build_flags = ... -D DISCORD_TLS_INSECURE
```

**生產環境（CA 驗證）：**
1. 下載 Discord 使用的根 CA 憑證（通常為 DigiCert Global Root CA）
2. 轉換為 PEM 格式，每行以 `\n` 結尾
3. 在 `platformio.ini` 定義：

```ini
build_flags = -I include -D FACEGUARD
              -D BOARD_HAS_PSRAM -mfix-esp32-psram-cache-issue
              -D DISCORD_ROOT_CA_CERT='"-----BEGIN CERTIFICATE-----\nMIIB...\n-----END CERTIFICATE-----\n"'
```

### Discord Webhook 設定

1. 在 Discord 伺服器中：「頻道設定」→「整合」→「Webhook」→「新建 Webhook」
2. 複製 Webhook URL
3. 透過 WebUI `/settings` 貼上 URL（Config Portal 僅設定 WiFi，不支援 Discord URL）
4. 測試：透過 Serial `u` 指令觸發未知訪客事件，確認 Discord 頻道收到訊息

### MQTT Broker 設定

若與 Agent 2 協作，需部署 MQTT Broker：

**建議方案（LAN 內部）：**

```bash
# Raspberry Pi / 本機安裝 Mosquitto
sudo apt install mosquitto mosquitto-clients
sudo systemctl enable mosquitto
```

**最小 Mosquitto 設定（`/etc/mosquitto/mosquitto.conf`）：**

```
listener 1883 0.0.0.0
allow_anonymous true
```

設定完成後，透過 WebUI `/settings` 輸入 Broker IP 與 Port。

---

## 部署步驟

### Step 1：編譯

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e faceguard
```

確認輸出結尾：
```
RAM:   [...] 
Flash: [...]
====== [SUCCESS] Took X.XX seconds ======
```

### Step 2：燒錄韌體

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e faceguard -t upload --upload-port COM3
```

> 若板子不自動進入燒錄模式，手動按住 BOOT 鍵同時按 RST 鍵。

### Step 3：首次設定（Config Portal）

1. 開啟串列監視器，確認開機日誌
2. 連線至 AP `FaceGuard-Setup`（密碼：`faceguard99`）
3. 瀏覽 `http://192.168.4.1`，設定 WiFi SSID 與密碼（Config Portal 只負責 WiFi）
4. 儲存 → 裝置重啟連線 WiFi；Discord URL 等其他設定待 WiFi 連線後透過 WebUI `/settings` 設定

### Step 4：驗證 WiFi 連線

Serial 輸出：
```
[FaceGuard] WiFi connected. IP: 192.168.x.x  MAC: xx:xx:xx:xx:xx:xx
[FaceGuard] mDNS: faceguard.local
[FaceGuard] NTP sync OK
```

### Step 5：首次登入 Dashboard

1. 瀏覽 `http://faceguard.local/` 或 `http://<IP>/`
2. 輸入帳號 `admin`
3. 首次登入後強制設定新密碼（8–64 字元）

### Step 6：MQTT 設定（若使用 Agent 2）

1. 前往 WebUI `/settings`
2. 輸入 MQTT Broker IP 與 Port
3. 儲存 → 裝置自動連線 MQTT

Serial 確認：
```
[FaceGuard] Agent2 MQTT connected
```

### Step 7：霍爾感應器校準

1. 安裝門磁感應器與磁鐵
2. 關閉門（磁鐵吸附），Serial 輸入 `H`：自動偵測偏移方向，更新對應的 lo 或 hi 邊界
3. Serial 輸入 `h` 確認校準結果（顯示 raw、lo、hi、hyst、door 狀態）
4. 開關門測試，確認 `door OPEN/CLOSED` 正確切換

### Step 8：人臉註冊

1. 前往 WebUI `/dashboard`，在人臉管理區塊輸入使用者名稱，點擊「Enroll Face」
3. 站在 Camera 前（正面、30–80 cm）
4. 等待「已儲存」確認訊息
5. 重複步驟 2–4 最多 7 位使用者

### Step 9：功能驗證

| 測試項目 | 方法 | 預期結果 |
|---------|------|---------|
| 門感應 | 開關門 | Serial 印出 door OPEN/CLOSED，LED 無變化 |
| 人臉辨識 | 站在 Camera 前 8 秒 | Serial 印出 KNOWN_CONFIRMED，LED 轉綠 |
| 未知訪客 | Serial 輸入 `u` | 蜂鳴器響，LED 閃爍，Discord 收到訊息 |
| Dashboard | 瀏覽 WebUI | 即時狀態更新（3s 輪詢） |
| MJPEG 串流 | 瀏覽 `:81/stream` | 即時 Camera 畫面 |

---

## 網路規劃

### 建議 LAN 設定

```
路由器（192.168.1.1）
  │
  ├── ESP32 FaceGuard (192.168.1.100 或 DHCP 分配)
  │     mDNS: faceguard.local
  │     HTTP: port 80
  │     MJPEG: port 81
  │
  ├── MQTT Broker (192.168.1.10)
  │     port 1883
  │
  └── Agent 2 (192.168.1.101)
        mDNS: agent2.local
```

### DHCP 預留 IP（建議）

為避免 IP 改變影響 MQTT 設定，在路由器為 ESP32 MAC 位址預留固定 IP。

ESP32 MAC 位址可從 Serial 開機日誌取得：
```
[FaceGuard] WiFi connected. IP: 192.168.1.100  MAC: AA:BB:CC:DD:EE:FF
```

### mDNS

同網段的裝置可透過 `faceguard.local` 存取（無需知道 IP）：
- `http://faceguard.local/`：Dashboard
- `http://faceguard.local:81/stream`：MJPEG 串流

> mDNS 在某些 Android 設備或路由器設定下可能不可用，此時需使用 IP 直連。

---

## 安全性強化

### 目前限制

| 風險 | 說明 | 緩解方式 |
|------|------|---------|
| HTTP 無加密 | Dashboard 使用 HTTP | 僅限 LAN 使用，不暴露至公網 |
| CSRF 防護 | 有，per-boot token | — |
| Session 劫持 | LAN 內嗅探 | 確保 LAN 不含不信任裝置 |
| Webhook URL 外洩 | Log 中只印最後 8 字元 | — |
| 暴力破解 | 5 次失敗 60s 鎖定 | — |

### 強制措施

1. **修改預設密碼**：首次登入強制設定（不可使用空密碼）
2. **勿暴露至公網**：若需遠端存取，使用 VPN（WireGuard/Tailscale）
3. **生產環境移除 `DISCORD_TLS_INSECURE`**：改用 CA 驗證
4. **Discord Webhook URL 保密**：URL 即認證，不應共享

---

## 監控與維護

### Discord 通知類型

| 事件 | 觸發條件 |
|------|---------|
| 開機公告 | 每次 ESP32 重啟，發送 IP 位址 |
| 已知使用者回家 | KNOWN_CONFIRMED + 門開啟 |
| 未知訪客警報 | UNKNOWN_CONFIRMED + ALERT_RED |

### WebUI 監控

| 頁面 | 監控內容 |
|------|---------|
| `/dashboard` | 即時系統狀態（3s 更新） |
| `/log/alert` | 警戒事件歷史 |
| `/log/door` | 門禁記錄 |
| `/log/face` | 人臉辨識記錄 |

### 自動重啟條件

裝置在以下情況自動重啟（`ESP.restart()`）：

| 條件 | 觸發時間 |
|------|---------|
| Config Portal 無操作 | 5 分鐘 |
| WiFi 持續斷線 | 5 分鐘 |
| 手動清除 WiFi 憑證 | 立即（Serial `W`） |

---

## 韌體更新流程

1. 確認當前設定（記錄 MQTT Broker、Discord URL 等）
2. 編譯新韌體
3. **不要 Erase Flash**（保留 NVS 設定）
4. 燒錄新韌體
5. 監視 Serial 確認正常開機
6. 驗證功能（Dashboard、人臉辨識、MQTT）

> 若需清除 NVS（設定不相容），在 Step 3 執行 `pio run -e faceguard -t erase`，燒錄後重新設定所有項目。

---

## 故障排除

### 裝置無法開機或持續重啟

1. 確認 USB 電源 ≥ 5V/2A
2. 檢查 GPIO 0（BOOT）是否被意外拉低
3. 監視 Serial 查看 panic 訊息

### Camera 初始化失敗

```
[FaceGuard] WARNING: camera init failed — face detection unavailable
```

1. 確認 Camera 排線接緊
2. 確認 `build_flags` 包含 `-D BOARD_HAS_PSRAM -mfix-esp32-psram-cache-issue`
3. 驗證板子有 PSRAM：Serial 輸入 `c` 查看狀態
4. 嘗試 `ESP.restart()` 後重試（某些板子 Camera 需二次初始化）

### WiFi 無法連線

1. 確認 SSID 和密碼正確
2. 確認 WiFi 為 2.4GHz（ESP32 不支援 5GHz）
3. 若持續失敗，Serial 輸入 `W` 清除憑證重新設定

### MQTT 無法連線

1. 確認 Broker IP 可從 FaceGuard 網段 ping 到
2. 確認 Broker 服務運行：`mosquitto_sub -h <ip> -t test`
3. 確認 port 1883 未被防火牆封鎖
4. 若 Broker 需認證，目前版本不支援（需修改 `AgentComm::_reconnect()`）

### Discord 通知未收到

1. 確認 Webhook URL 格式正確（`https://discord.com/api/webhooks/...`）
2. 確認 ESP32 可存取 internet（非 isolated LAN）
3. 查看 Serial 日誌：`[FaceGuard] discord sent (last 8: xxxxxxxx)` 或錯誤訊息
4. 確認 rate limit（同事件 30 秒內只送一次）
5. 確認非 5 分鐘 cooldown 期間（連線失敗後）
6. **生產環境**：確認 TLS 設定正確（非 INSECURE 模式下需 CA 憑證）

### 人臉辨識準確率低

1. 確認光線充足均勻（避免逆光、強烈側光）
2. WebUI `/api/status` 查看 `face_tex`（texture score）：若 < 20，考慮降低 `FACE_TEXTURE_MIN_STDDEV`
3. 重新在不同光線條件下多次註冊（清除後重新全部註冊）
4. 若同一人常被誤認為 UNKNOWN，降低 `FACE_SIMILARITY_THRESHOLD`（如 0.88）

### 霍爾感應器不穩定

1. 確認外接 10kΩ 上拉電阻（GPIO 33 雖有內部上拉，但建議外接以增強穩定性）
2. 感應器與磁鐵距離過大 → 縮短距離或更換靈敏度更高的感應器
3. 調整 `DOOR_DEBOUNCE_MS` 增加去彈跳時間（預設 200ms）
4. 重新校準閾值（Serial `H` 鍵）

---

## 錯誤處理行為參考

| 錯誤情況 | FaceGuard 行為 | 恢復方式 |
|---------|-------------|---------|
| WiFi 斷線 | 持續警戒，MQTT 暫停，重連中 | WiFi 恢復後自動重連 |
| WiFi 斷線 > 5 分鐘 | 自動重啟進 Config Portal | 重新連線 WiFi |
| NTP 同步失敗 | 繼續運作，log 標記相對時間 | 背景持續重試 |
| Camera 初始化失敗 | FACE_NO_FACE，繼續門禁/警戒 | 重啟後重試 |
| Agent 2 離線 | ALERT_RED 獨立警戒 | Agent 2 重連後恢復協作 |
| MQTT 斷線 | 本機警戒不中斷，背景重連 | 自動重連後重新 subscribe |
| Discord 失敗 | 記錄失敗狀態，5 分鐘後重試 | cooldown 結束後恢復 |
| NVS 寫入失敗 | 記錄錯誤，繼續運作 | 手動重試或重啟 |
