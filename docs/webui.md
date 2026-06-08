# WebUI 與 API 說明

## 目錄

- [存取方式](#存取方式)
- [頁面一覽](#頁面一覽)
- [認證機制](#認證機制)
- [頁面說明](#頁面說明)
- [REST API](#rest-api)
- [CSRF 保護](#csrf-保護)
- [Session 規格](#session-規格)

---

## 存取方式

| 方式 | URL |
|------|-----|
| IP 直連 | `http://<裝置IP>/` |
| mDNS（同網段） | `http://faceguard.local/` |
| MJPEG 串流 | `http://<裝置IP>:81/stream` |

> **安全警告**：Dashboard 使用 HTTP（非 HTTPS）。僅供區域網路內使用，**切勿暴露至公網**。

---

## 頁面一覽

| 路徑 | 需要登入 | CSRF 保護 | 說明 |
|------|---------|----------|------|
| `/` | — | — | 導向 `/dashboard` |
| `/login` | — | — | 登入表單 |
| `/logout` | session | — | 登出，清除 session |
| `/dashboard` | session + 需改密碼 | — | 即時狀態（3s AJAX 輪詢）+ 人臉管理 |
| `/settings` | session + 需改密碼 | POST 需驗證 | 統一設定頁（Discord、MQTT、霍爾雙邊界、時區、蜂鳴器、密碼） |
| `/settings/save` | session + 需改密碼 | POST 需驗證 | 儲存設定（表單 action） |
| `/password/change` | session | — | 首次登入強制密碼修改頁 |
| `/password/save` | session | POST 需驗證 | 儲存新密碼 |
| `/log/door` | session + 需改密碼 | — | 門禁紀錄（RAM 最近 50 筆 + SPIFFS 月份切換） |
| `/log/face` | session + 需改密碼 | — | 人臉辨識紀錄（RAM 最近 50 筆 + SPIFFS 月份切換） |
| `/log/alert` | session + 需改密碼 | — | 警戒事件紀錄（RAM 最近 50 筆 + SPIFFS 月份切換） |
| `/analytics` | session + 需改密碼 | — | 統計分析頁面（門次、人臉排行、峰值時段） |
| `/api/status` | session + 需改密碼 | — | JSON 即時狀態（詳見下方） |
| `/api/face/enroll` | session + 需改密碼 | POST 需驗證 | 觸發人臉註冊 API |
| `/api/face/list` | session + 需改密碼 | — | 列出已註冊人臉名稱 |
| `/api/face/clear` | session + 需改密碼 | POST 需驗證 | 清除所有人臉資料 API |
| `/api/log/door` | session + 需改密碼 | — | 門禁紀錄 JSON（RAM 最近 50 筆） |
| `/api/log/face` | session + 需改密碼 | — | 人臉辨識紀錄 JSON |
| `/api/log/alert` | session + 需改密碼 | — | 警戒事件紀錄 JSON |
| `/api/log/door/paged` | session + 需改密碼 | — | SPIFFS 分頁（?month=YYYYMM&page=N&per_page=20） |
| `/api/log/face/paged` | session + 需改密碼 | — | SPIFFS 分頁 |
| `/api/log/alert/paged` | session + 需改密碼 | — | SPIFFS 分頁 |
| `/api/log/stats` | session + 需改密碼 | — | 月份統計 JSON（?month=YYYYMM） |
| `/api/log/months` | session + 需改密碼 | — | 有資料月份列表 JSON |
| `/api/buzzer/test` | session | POST 需驗證 | 測試蜂鳴器（form body `freq` 可選） |
| `/snapshot` | **不需要** | — | 擷取單張 JPEG 畫面（供 Agent 2 等 LAN 裝置使用；無認證保護，程式碼未強制限制來源 IP，建議確保裝置不暴露至公網） |

> **WiFi 設定**：Dashboard 不提供 WiFi 修改頁面。WiFi 憑證只能透過 Config Portal（AP 模式）設定。若需變更 WiFi，透過 Serial 輸入 `W` 清除憑證並重啟，裝置會進入 AP 模式。

---

## 認證機制

### 登入流程

1. 訪問需要認證的頁面 → 自動重導向 `/login`
2. 輸入密碼（預設 `admin`；登入表單僅有密碼欄位，無帳號欄位）
3. 密碼錯誤 5 次 → 鎖定 60 秒
4. 登入成功 → 建立 session token，設定 Cookie

### Cookie 格式

```
Set-Cookie: sid=<16-byte-random-hex>; HttpOnly; Path=/; SameSite=Lax
```

### 密碼儲存

- 密碼以 `salted-SHA-256` hash 儲存於 NVS `dashboard_pw`
- 首次登入必須修改預設密碼（`hasDefaultPassword()` 回傳 true 時強制跳轉）

---

## 頁面說明

### Dashboard（`/dashboard`）

每 3 秒透過 AJAX 呼叫 `/api/status` 更新顯示：

| 顯示項目 | 說明 |
|---------|------|
| WiFi 狀態 | 已連線 / SSID / IP |
| 時間 | NTP 同步狀態與當前時間 |
| Door State | OPEN / CLOSED |
| Face State | NO_FACE / KNOWN / UNKNOWN |
| Alert Level | GREEN / YELLOW / RED（含顏色指示） |
| Agent 2 狀態 | online / offline（online 時顯示 "Online · ?"，因 `presence_state` 目前實作中不回傳實際佔用狀態） |
| 已知使用者 | 最後一次 KNOWN_CONFIRMED 的使用者名稱 |
| 最近事件 | 最近 5 筆 Face/Door/Alert 事件摘要 |
| FaceVoter 狀態 | known hits、unknown hits、elapsed time |
| AI 統計 | Camera 最後偵測時間、texture score、similarity |

### 統一設定頁（`/settings`）

所有設定集中在一個頁面，送出至 `/settings/save`。包含：

| 設定項目 | 說明 | 限制 |
|---------|------|------|
| Discord Webhook URL | Discord 通知 Webhook | 需以 `https://discord.com/api/webhooks/` 或 `https://discordapp.com/api/webhooks/` 開頭；最多 256 字元；清空 = 停用 |
| MQTT Broker | IP 或 hostname | 最多 63 字元（HTML 層限制）；空值 = 停用 MQTT |
| MQTT Port | 埠號 | 1–65535（HTML 層限制）；預設 1883 |
| MQTT Username | 認證用帳號 | 最多 63 字元；空值 = 無認證 |
| MQTT Password | 認證用密碼 | 最多 63 字元；留空不更新；Username 清空時連帶清除 |
| 霍爾 Open Zone 下界（hall_lower） | ADC open zone 下界 | 0–4095；OPEN 觸發：raw > hall_lower + 150 |
| 霍爾 Open Zone 上界（hall_upper） | ADC open zone 上界 | 0–4095；OPEN 觸發：raw < hall_upper - 150 |
| 時區設定（tz_offset） | NTP 及 MQTT timestamp 的 UTC offset | UTC-12:00 至 UTC+14:00 整時或半時；預設 UTC+8；即時生效（不需重啟） |
| 蜂鳴器頻率（Hz） | 警報音頻率 | 200–8000；預設 2000 |
| 蜂鳴器持續時間（秒） | 警報蜂鳴持續時間 | 10–300 秒；儲存為 ms；預設 60 |
| Dashboard 密碼 | 新密碼（需輸入兩次） | 8–64 字元 ASCII printable（不可全為空格）；空白不更新 |

> **MQTT 設定套用**：MQTT broker / port / auth 設定儲存後需**重啟裝置**才生效；設定頁面會顯示 `MQTT settings saved (restart to apply).`。  
> WiFi SSID / 密碼**不**在此設定。WiFi 憑證只能透過 Config Portal（AP 模式）設定。若需變更 WiFi，透過 Serial 輸入 `W` 清除憑證並重啟。

### Dashboard（`/dashboard`）

Dashboard 整合 Camera 預覽（port 81 MJPEG stream）與人臉管理，每 3 秒 AJAX 更新：

- Alert Level（含警報閃爍狀態）、Door State、Agent 2 狀態、Last Known User
- Camera 即時畫面（port 81 MJPEG）+ 辨識狀態 + FaceVoter 投票進度
- 人臉管理：輸入名稱 → Enroll Face / Clear All

### 日誌頁面

- `/log/face`、`/log/door`、`/log/alert` 各顯示 RAM 最近 50 筆記錄
- 支援 SPIFFS 月份切換：選擇月份後透過 `/api/log/*/paged` 分頁載入歷史記錄
- 資料格式：動態產生表格，含 timestamp、事件類型、相關資訊
- 頁面刷新顯示最新記錄（無自動輪詢）

---

## REST API

### GET `/api/status`

需要 session 認證。回傳系統即時狀態 JSON。

**Response（HTTP 200）：**

```json
{
  "alert_level": "ALERT_RED",
  "door_state": "DOOR_CLOSED",
  "agent2_online": false,
  "alarm_active": false,
  "last_known_user": "",
  "presence_state": "",
  "uptime": 12345,
  "hall_raw": 850,
  "hall_lower": 1000,
  "hall_upper": 3000,
  "face_count": 2,
  "face_max": 7,
  "face_result": "NONE",
  "face_name": "",
  "face_sim": 0.0,
  "face_tex": 0.0,
  "face_voter_state": "idle",
  "face_voter_confirmed_name": "",
  "face_voter_known_count": 0,
  "face_voter_known_min": 3,
  "face_voter_unknown_hits": 0,
  "face_voter_unknown_elapsed_s": 0,
  "face_voter_unknown_window_s": 10
}
```

`face_result` 可能值：`"KNOWN"`、`"UNKNOWN"`、`"DETECTED"`、`"NONE"`（FACE_RECENT_MS 內無偵測時）  
`face_voter_state` 可能值：`"idle"`、`"active"`、`"known_pending"`、`"unknown_pending"`、`"known_confirmed"`  
`presence_state` 目前實作中永遠回傳空字串（`""`）；Agent 2 在線狀態可由 `agent2_online` 布林值判斷。

### POST `/api/face/enroll`

需要 session 認證。觸發人臉註冊模式。

**Request Body（form-urlencoded）：**

```
csrf=<csrf-token>&name=Alice
```

| 欄位 | 說明 |
|------|------|
| `csrf` | CSRF token（必填） |
| `name` | 使用者名稱，最多 16 字元，僅接受 ASCII 32–126；選填，空白則匿名 |

**Response（HTTP 200）：**

```json
{"scheduled": true, "count": 2, "max": 7}
```

**Response（HTTP 409）：**（face bank full）

```json
{"error": "face bank full"}
```

**Response（HTTP 503）：**（camera not ready）

```json
{"error": "camera not ready"}
```

**Response（HTTP 403）：**（CSRF 驗證失敗）

```json
{"error": "CSRF"}
```

### GET `/api/face/list`

需要 session 認證。列出所有已註冊人臉名稱。

**Response（HTTP 200）：**

```json
{"faces": ["Alice", "Bob", ""]}
```

空字串表示匿名（未設定名稱）。

### POST `/api/face/clear`

需要 session 認證。清除 NVS 中所有已儲存的人臉資料，並重置 FaceVoter。

**Request Body（form-urlencoded）：**

```
csrf=<csrf-token>
```

**Response（HTTP 200）：**

```json
{"cleared": true}
```

**Response（HTTP 500）：**（NVS 寫入失敗）

```json
{"cleared": false, "error": "NVS write failed"}
```

### GET `/api/log/door` / `/api/log/face` / `/api/log/alert`

需要 session 認證。回傳 RAM 環形緩衝區最近 50 筆記錄 JSON。

**Response（HTTP 200）：**

```json
[
  {"timestamp": "2025-12-20T18:30:05+08:00", "door_state": "DOOR_OPEN", "related_user": "Alice"},
  ...
]
```

### GET `/api/log/door/paged` / `/api/log/face/paged` / `/api/log/alert/paged`

需要 session 認證。從 SPIFFS 讀取指定月份的分頁紀錄。

**Query Parameters：**

| 參數 | 說明 | 預設 |
|------|------|------|
| `month` | 月份（YYYYMM 數字） | 當前月份 |
| `page` | 頁碼（1 起） | 1 |
| `per_page` | 每頁筆數（1–50） | 20 |

**Response（HTTP 200）：**

```json
{"total": 150, "page": 1, "per_page": 20, "data": [...]}
```

### GET `/api/log/stats`

需要 session 認證。回傳指定月份的統計摘要（含每日分布、峰值時段、人臉排行榜）。

**Query Parameters：** `?month=YYYYMM`（省略則為當前月份）

### GET `/api/log/months`

需要 session 認證。回傳 SPIFFS 上有資料的月份列表（降序）。

**Response（HTTP 200）：**（bare JSON array）

```json
[202512, 202511, 202510]
```

### POST `/api/buzzer/test`

需要 session 認證（不需 `需改密碼`）。發出一聲測試嗶聲。警報啟動中拒絕執行（HTTP 409）。

**Request Body（form-urlencoded）：**

```
csrf=<csrf-token>&freq=2000
```

| 欄位 | 說明 |
|------|------|
| `csrf` | CSRF token（必填） |
| `freq` | 頻率 Hz，選填；200–8000；省略則使用 NVS 設定值 |

**Response（HTTP 200）：** `OK`

**Response（HTTP 409）：** `Alarm active`（警報進行中）

---

## CSRF 保護

所有改變系統狀態的 POST 請求（設定頁面）皆需包含 CSRF token。

### Token 取得方式

HTML 表單中自動嵌入（欄位名稱為 `csrf`）：

```html
<input type="hidden" name="csrf" value="<token>">
```

或透過 `SessionAuth::getCsrfToken()` 在後端取得並嵌入。

### Token 驗證

```
POST 請求 → DashboardServer 路由處理
  → SessionAuth::verifyCsrf(server.arg("csrf"))
  → 若不符合 → HTTP 403
```

CSRF token 為 per-boot random hex，重啟後重新生成。

---

## Session 規格

| 項目 | 值 |
|------|-----|
| Token 長度 | 32 字元（16-byte random hex） |
| TTL | 30 分鐘（每次授權請求重置） |
| 儲存方式 | in-memory（重啟後失效） |
| Cookie 屬性 | `HttpOnly; Path=/; SameSite=Lax` |
| 最大同時 session | 1（新登入覆蓋舊 session） |
| Brute-force 防護 | 5 次失敗鎖定 60 秒 |

### 安全限制

1. **無 HTTPS**：Cookie 不加 `Secure` 標記，僅適用區域網路
2. **單 session**：一次只允許一個有效 session
3. **重啟失效**：session 不持久化，每次重啟需重新登入
