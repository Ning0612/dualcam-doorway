# 開發指南

## 目錄

- [環境需求](#環境需求)
- [安裝 PlatformIO](#安裝-platformio)
- [Clone 專案](#clone-專案)
- [建置（Build）](#建置build)
- [燒錄（Flash）](#燒錄flash)
- [串列監視器](#串列監視器)
- [Serial 除錯指令](#serial-除錯指令)
- [開發注意事項](#開發注意事項)
- [常見問題](#常見問題)

---

## 環境需求

| 項目 | 版本 / 說明 |
|------|------------|
| OS | Windows 10/11、macOS、Linux |
| Python | ≥ 3.8（PlatformIO 依賴） |
| PlatformIO Core | ≥ 6.x |
| ESP-IDF（由 PlatformIO 管理） | Espressif32 Arduino 框架 |

---

## 安裝 PlatformIO

### 方法 A：VS Code 擴充套件（推薦）

1. 安裝 [VS Code](https://code.visualstudio.com/)
2. 在 Extensions 搜尋 `PlatformIO IDE` 並安裝
3. 重啟 VS Code

### 方法 B：CLI 安裝

```bash
pip install platformio
```

---

## Clone 專案

```bash
git clone https://github.com/Ning0612/esp32-faceguard faceguard
cd faceguard
```

---

## 建置（Build）

### CI

GitHub Actions 會在 push、pull request 與手動 workflow dispatch 時執行
`.github/workflows/ci.yml`：

- 安裝 PlatformIO
- 執行 `pio run -e faceguard`

CI 只驗證韌體可建置，不會燒錄裝置、啟動相機、連接 MQTT/Discord，
也不會進行人臉辨識準確率量測。

### Windows（PowerShell）

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e faceguard
```

### macOS / Linux

```bash
~/.platformio/penv/bin/pio run -e faceguard
```

> **重要**：`pio` 通常不在 PATH。務必使用完整路徑。

建置成功後，輸出檔案位於：
- `.pio/build/faceguard/firmware.bin`
- `.pio/build/faceguard/firmware.elf`

### 依賴套件

`platformio.ini` 中定義的依賴，首次建置時自動下載：

| 套件 | 版本 | 用途 |
|------|------|------|
| ArduinoJson | ^7 | JSON 序列化（MQTT payloads、API 回應） |
| PubSubClient | ^2.8 | MQTT client |
| Adafruit NeoPixel | ^1.12 | WS2812B RGB LED 驅動 |

---

## 燒錄（Flash）

### 確認 COM 埠

**Windows：**
```powershell
# 列出 COM 埠
[System.IO.Ports.SerialPort]::getportnames()
```

或在裝置管理員中查看「連接埠（COM 和 LPT）」。

**macOS / Linux：**
```bash
ls /dev/tty.*
ls /dev/ttyUSB*
```

### 燒錄指令

```powershell
# Windows
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e faceguard -t upload --upload-port COM3

# 若不指定埠號，PlatformIO 會自動偵測
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e faceguard -t upload
```

### 燒錄失敗排除

1. **Upload 前無法進入 bootloader**：按住 BOOT 按鈕同時按 RST
2. **找不到 COM 埠**：確認 USB 驅動（CH340 或 CP2102）已安裝
3. **PSRAM 相關錯誤**：確認 `build_flags` 包含 `-D BOARD_HAS_PSRAM -mfix-esp32-psram-cache-issue`

---

## 串列監視器

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor --port COM3
```

波特率：`115200`（已在 `platformio.ini` 設定）

### 開機日誌範例

```
[FaceGuard] boot
[FaceGuard] WARNING: Dashboard HTTP only — LAN use only, not internet-safe.
[FaceGuard] WiFi connected. IP: 192.168.1.100  MAC: AA:BB:CC:DD:EE:FF
[FaceGuard] mDNS: faceguard.local
[FaceGuard] NTP sync OK
[FaceGuard] Hall bounds: lo=1000 hi=3000
[FaceGuard] HTTP server on port 80
[FaceGuard] ready
[FaceGuard]   h=Hall value  H=auto-calibrate bounds  d=door toggle
[FaceGuard]   u=unknown     e=enroll face     r=clear faces
[FaceGuard]   n=face count  c=camera status   s=full status
[FaceGuard]   W=clear WiFi credentials
```

---

## Serial 除錯指令

在串列監視器中直接輸入單一字元觸發：

| 按鍵 | 功能 |
|------|------|
| `h` | 印出霍爾感應器當前 raw、lo/hi 邊界、hysteresis、門狀態 |
| `H` | 自動校準（門關閉、磁鐵吸附時按下）：依偏移方向更新 lo 或 hi，偏移量 = raw ± 2×HYSTERESIS |
| `d` | 手動切換門狀態（OPEN ↔ CLOSED），無需硬體 |
| `u` | 手動觸發「未知訪客 CONFIRMED」（測試警報流程） |
| `e` | 排程下一張臉為人臉註冊（10 秒視窗） |
| `r` | 取消待機中的 enroll 並清除所有已儲存人臉 |
| `n` | 印出已註冊人臉數量 |
| `c` | 印出 Camera 初始化狀態與最後偵測時間 |
| `s` | 印出完整系統狀態（alert level、door、agent2、alarm） |
| `W` | 清除 WiFi 憑證並重啟（進入 Config Portal） |

---

## 開發注意事項

### Build Flags

`platformio.ini` 的 `[env:faceguard]` 區段：

```ini
build_flags = -I include -D FACEGUARD -D DISCORD_TLS_INSECURE
              -D BOARD_HAS_PSRAM -mfix-esp32-psram-cache-issue
```

| 旗標 | 說明 |
|------|------|
| `-I include` | 讓 `lib/` 下的模組可 include `config.h`、`states.h` 等 |
| `-D FACEGUARD` | 條件編譯標記（保留未來 Agent 2 共用程式碼用） |
| `-D DISCORD_TLS_INSECURE` | 跳過 TLS 憑證驗證（開發用，**生產環境必須移除**） |
| `-D BOARD_HAS_PSRAM` | 啟用 PSRAM（YUV422 camera frame 需要） |
| `-mfix-esp32-psram-cache-issue` | 修復 AI Thinker 系列 PSRAM cache 問題 |

### 生產環境 TLS 設定

開發完成後，應移除 `DISCORD_TLS_INSECURE` 並改用 CA 憑證：

```ini
; 生產環境 build_flags
build_flags = -I include -D FACEGUARD
              -D BOARD_HAS_PSRAM -mfix-esp32-psram-cache-issue
              -D DISCORD_ROOT_CA_CERT='"-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"'
```

### FreeRTOS Task

主程式用到四個 FreeRTOS Task：

| Task 名稱 | Stack | 優先權 | Core | 用途 |
|-----------|-------|--------|------|------|
| `boot_notify` | 8192 B | 1 | 任意 | 開機 Discord 通知（5s 延遲後發送，執行後自刪） |
| `cam_init` | 8192 B | 1 | 任意 | Camera 非同步初始化（執行後自刪） |
| `mqtt_comm` | 8192 B | 1 | 0 | MQTT loop、重連、DNS 解析、Agent 2 超時監控 |
| `cam_stream` | 8192 B | 1 | 0 | MJPEG frame 推送（port 81，由 `CameraAgent::startStreamServer()` 啟動） |

---

## 常見問題

### Q: `pio` 找不到指令

**A:** PlatformIO 的 `pio` 二進位不在系統 PATH。使用完整路徑：
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" <command>
```

### Q: Camera 初始化失敗

**A:** 確認：
1. `BOARD_HAS_PSRAM` 旗標已設定
2. 板子有 PSRAM（NMK99、AI Thinker ESP32-CAM）
3. Camera 排線確實插入，無鬆脫

### Q: 人臉辨識準確率低

**A:** 參考 [人臉辨識文件](face-recognition.md) 的調校章節。常見原因：光線不足、texture stddev 過低（`FACE_TEXTURE_MIN_STDDEV` 需調整）、`FACE_SIMILARITY_THRESHOLD` 過高。

### Q: MQTT 未連線

**A:** 確認 `mqtt_broker` NVS 已設定（透過 WebUI `/settings`），Broker IP 可從 FaceGuard 所在網路存取，port 1883 已開放。

### Q: 編譯出現 `DISCORD_ROOT_CA_CERT` 或 `DISCORD_TLS_INSECURE` 錯誤

**A:** `DiscordNotifier.cpp` 要求至少定義其中一個。開發環境加 `-D DISCORD_TLS_INSECURE`；生產環境改用 `-D DISCORD_ROOT_CA_CERT="..."`.
