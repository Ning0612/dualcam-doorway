# 硬體接線說明

## 目錄

- [硬體清單](#硬體清單)
- [GPIO 配置總覽](#gpio-配置總覽)
- [OV2640 Camera 接線](#ov2640-camera-接線)
- [WS2812B RGB LED](#ws2812b-rgb-led)
- [霍爾感應器（Hall Effect Sensor）](#霍爾感應器hall-effect-sensor)
- [壓電蜂鳴器](#壓電蜂鳴器)
- [安裝注意事項](#安裝注意事項)
- [GPIO 禁用清單](#gpio-禁用清單)

---

## 硬體清單

| 元件 | 型號/規格 | 說明 |
|------|----------|------|
| 主控板 | ESP32 NMK99 | AI Thinker ESP32-CAM 相容，內建 PSRAM |
| 相機模組 | OV2640 | FPC 排線接 NMK99，QQVGA 模式 |
| RGB LED | WS2812B（單顆） | 可定址 NeoPixel，GRB 像素順序 |
| 門磁感應器 | 霍爾效應感應器（模組） | 輸出 0–3.3V 類比電壓 |
| 蜂鳴器 | 壓電式無源蜂鳴器 | 3.3V 直驅 |
| 電阻 | 10 kΩ（×1） | 霍爾感應器上拉電阻（若需要） |
| USB 電源 | 5V / 2A | ESP32-CAM 供電 |

---

## GPIO 配置總覽

```
ESP32 NMK99 (AI Thinker / NMK99 相容)

                ┌────────────────────┐
         3V3 ─ │ 3V3            GND │ ─ GND
         GND ─ │ GND            IO1 │
              ─ │ IO0*          IO3  │
              ─ │ IO2           IO4  │
              ─ │ IO4           IO16 │
              ─ │ IO12*         IO17 │
   PIN_BUZZER ─ │ IO13     CAM │ IO18 │
              ─ │ IO14     CAM │ IO19 │
              ─ │ IO15     CAM │ IO21 │
       5V IN  ─ │ 5V       CAM │ IO22 │
              ─ │ IO16     CAM │ IO23 │
              ─ │ IO17     CAM │ IO25 │*CAM VSYNC
  PIN_LED_DATA─ │ IO32     CAM │ IO26 │*CAM SIOD
    PIN_HALL   ─ │ IO33     CAM │ IO27 │*CAM SIOC
              ─ │ IO34*    CAM │ IO35*│
              ─ │ IO36*    CAM │ IO39*│
                └────────────────────┘
                  * 特殊用途或限制，見下文
```

### 實際使用腳位

| 腳位 | 定義常數 | 方向 | 說明 |
|------|---------|------|------|
| GPIO 32 | `PIN_LED_DATA` | OUTPUT | WS2812B 資料線 |
| GPIO 13 | `PIN_BUZZER` | OUTPUT | 壓電蜂鳴器 |
| GPIO 33 | `PIN_HALL` | ADC INPUT | 霍爾感應器（ADC1_CH5） |

---

## OV2640 Camera 接線

NMK99 / AI Thinker ESP32-CAM 的 OV2640 Camera **透過板載 FPC 排線直接接上**，不需另外接線。

Camera 佔用以下 GPIO，這些腳位**不可作為一般 I/O 使用**：

| GPIO | Camera 功能 |
|------|------------|
| 0 | XCLK（時脈輸出） |
| 5 | D0（資料） |
| 18 | D1 |
| 19 | D2 |
| 21 | D3 |
| 22 | PCLK（像素時脈） |
| 23 | HREF（水平同步） |
| 25 | VSYNC（垂直同步） |
| 26 | SIOD / I2C SDA |
| 27 | SIOC / I2C SCL |
| 34 | D6 |
| 35 | D7 |
| 36 | D4 |
| 39 | D5 |

> **GPIO 32（CAM_PWDN）**：在標準 AI Thinker 板上此腳為 PWDN（Camera 電源控制）。NMK99 上 PWDN 未接線，因此 GPIO 32 可用於 WS2812B LED 資料線。程式中 `camera_config_t.pin_pwdn = -1`。

---

## WS2812B RGB LED

### 電氣規格

- 工作電壓：5V（LED 本體），資料訊號 3.3V 相容
- 資料協定：單線 NZR（800 kbps）
- 像素順序：**GRB**（Green-Red-Blue）

### 接線方式

```
ESP32 GPIO32 ──────────────────► DIN（WS2812B 資料輸入）
ESP32 5V     ──────────────────► VCC（WS2812B 電源）
ESP32 GND    ──────────────────► GND（WS2812B 接地）
```

> 若使用多顆串接，在 VCC 與 GND 之間加 100–470µF 電解電容，防止電源突波。

### 警戒顏色對應

| AlertLevel | LED 狀態 | 說明 |
|------------|----------|------|
| `ALERT_GREEN` | 綠色常亮 | 已知使用者 / 正常狀態 |
| `ALERT_YELLOW` | 黃色常亮 | Agent 2 回報室內有人，協調模式 |
| `ALERT_RED` | 紅色常亮 | Agent 2 離線或室內無人（預設） |
| `ALERT_RED` + alarm | 紅色閃爍（250ms） | 偵測到未知訪客，警報觸發 |

---

## 霍爾感應器（Hall Effect Sensor）

### 選擇與規格

- 建議使用線性霍爾感應器模組（如 A3144 或 OH3144 模組），輸出 0–3.3V 類比電壓
- 若使用 NPN 開路集極型（Active-LOW，接地時 = 磁鐵靠近），需外接上拉電阻

### 接線方式

```
              3.3V
               │
              10kΩ  ← 外部上拉電阻（若感應器為 Active-LOW，建議外接增強穩定性）
               │
GPIO 33 ────── ┤ ─── 感應器 SIGNAL 輸出
               
感應器 GND ──── ESP32 GND
感應器 VCC ──── ESP32 3.3V（或 5V，依模組規格）
```

> **重要**：GPIO 33 屬 ADC1（`ADC1_CH5`），WiFi 啟動後仍可正常使用。GPIO 34、35、36、39 為 **input-only 且無內部上拉**，使用 Active-LOW 訊號時**必須外接 10 kΩ 上拉電阻至 3.3V**，並使用 `INPUT`（非 `INPUT_PULLUP`）模式。本專案選用 GPIO 33 以支援 `INPUT_PULLUP`。

### 門磁安裝位置

```
門框（靜止端）          門板（移動端）
┌──────────────┐      ┌──────────────┐
│              │      │              │
│  霍爾感應器  │◄────►│   磁鐵       │
│  （固定）    │      │   （固定）   │
└──────────────┘      └──────────────┘
```

- 感應器與磁鐵相距 5–20mm 時感應（依模組靈敏度調整）
- 門關閉：磁鐵靠近感應器 → ADC 值低於閾值 → `DOOR_CLOSED`
- 門開啟：磁鐵遠離感應器 → ADC 值高於閾值 → `DOOR_OPEN`

### 閾值校準

1. 關閉門，在 Serial 輸入 `h` 查看 ADC 原始值
2. 開啟門，再次查看 ADC 原始值
3. 計算兩值中間點，輸入 `H` 將當前值儲存為閾值
4. 或透過 WebUI `/settings/system` 手動設定

預設閾值：`2048`（12-bit ADC 中點），Hysteresis：±`150`

---

## 壓電蜂鳴器

### 接線方式

```
ESP32 GPIO13 ──────────────► 蜂鳴器正極（+）
ESP32 GND    ──────────────► 蜂鳴器負極（−）
```

GPIO 13 支援 PWM，可發出不同頻率音調（`BuzzerController` 使用 `tone()` 驅動）。

---

## 安裝注意事項

1. **電源穩定性**：Camera 工作電流約 250mA，加上 WiFi 峰值可達 500mA。建議使用 5V/2A 以上電源。

2. **GPIO 0 boot-strap**：GPIO 0 在啟動時必須為 HIGH（否則進入下載模式）。出廠板通常已處理，自製接線需注意。

3. **GPIO 12 boot-strap**：GPIO 12 在啟動時必須為 LOW（決定 SPI Flash 電壓）。避免使用 GPIO 12 或確保啟動時不被拉高。

4. **ADC2 與 WiFi 衝突**：ADC2（GPIO 0、2、4、12–15、25–27）在 WiFi 啟動後無法使用。感應器訊號應接 ADC1（GPIO 32–39）腳位。

5. **Camera 與 GPIO 26/27 衝突**：GPIO 26（SIOD）和 GPIO 27（SIOC）被 Camera 佔用，不可作為一般 GPIO 或 I2C 使用。

---

## GPIO 禁用清單

以下 GPIO 在此專案中**禁止用於一般用途**：

| GPIO | 原因 |
|------|------|
| 0 | CAM_XCLK / boot-strap |
| 2 | boot-strap（可能影響燒錄） |
| 6–11 | 內部 SPI Flash |
| 12 | boot-strap（SPI 電壓選擇） |
| 5, 18–19, 21–23, 25–27, 34–36, 39 | Camera 佔用 |
