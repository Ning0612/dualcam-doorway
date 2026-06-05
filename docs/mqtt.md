# MQTT 協定說明

## 目錄

- [架構概覽](#架構概覽)
- [Broker 設定](#broker-設定)
- [連線規格](#連線規格)
- [Publish Topics（FaceGuard → Agent 2）](#publish-topicsfaceguard--agent-2)
- [Subscribe Topics（Agent 2 → FaceGuard）](#subscribe-topicsagent-2--faceguard)
- [訊息格式詳述](#訊息格式詳述)
- [離線行為](#離線行為)
- [Agent 2 通訊整合指南](#agent-2-通訊整合指南)

---

## 架構概覽

```
FaceGuard (ESP32 NMK99)           MQTT Broker              Agent 2 (室內 Agent)
    │                               │                           │
    │── home/security/door ─────────►──────────────────────────►│
    │── home/security/face ─────────►──────────────────────────►│
    │── home/security/alert ────────►──────────────────────────►│
    │── home/security/status ───────►──────────────────────────►│
    │── home/security/camera ───────►──────────────────────────►│  ← 5fps JPEG
    │                               │                           │
    │◄─ home/home_state/presence ───◄────────────────────────── │
    │◄─ home/home_state/alarm_decision ─◄───────────────────────│
    │◄─ home/home_state/alarm_command ──◄────────────────────────│  ← 主動指令
```

---

## Broker 設定

MQTT Broker 資訊儲存於 NVS，透過 WebUI `/settings` 設定：

| 設定項目 | NVS Key | 預設值 |
|---------|---------|-------|
| Broker IP/hostname | `mqtt_broker` | 空（未設定） |
| Port | `mqtt_port` | 1883 |
| Username | `mqtt_user` | 空（無認證） |
| Password | `mqtt_pw` | 空（無認證） |

若 Broker 未設定（空字串），`AgentComm::begin()` 為 no-op，FaceGuard 以 ALERT_RED 獨立運作。
Username 為空字串時，連線不帶認證資訊（Anonymous）。Username 清空時 Password 連帶清除。
**WebUI 認證更新規則**：Password 留空 = 保留 NVS 既有密碼（不清除）；Username 清空 = Username 與 Password 一併清除。

---

## 連線規格

| 項目 | 值 |
|------|-----|
| Client ID | `faceguard` |
| QoS | 0 |
| Keep-alive | 60 秒 |
| 重連間隔 | 5 秒（`MQTT_RECONNECT_MS`） |
| TCP socket 逾時 | 3 秒（`MQTT_SOCKET_TIMEOUT_S`，< FreeRTOS WDT 預設 5s） |
| 認證 | 可選；Username/Password 儲存於 NVS `mqtt_user`/`mqtt_pw` |
| Agent 2 離線判定 | 180 秒（3 分鐘）無 presence 訊息（`AGENT2_OFFLINE_TIMEOUT_MS`）；Agent 2 心跳約 60s |

> **注意**：Broker 連線成功 ≠ Agent 2 在線。Agent 2 在線狀態由 presence heartbeat 的到達時間決定，而非 MQTT broker 連線狀態。MQTT loop 與重連由背景 FreeRTOS task（Core 0）執行，`AgentComm::tick()` 僅在主 task 端排空事件佇列並觸發 callback。

---

## Publish Topics（FaceGuard → Agent 2）

### `home/security/door`

門狀態發生確認轉換時發布。

```json
{
  "agent": "FaceGuard",
  "timestamp": "2025-12-20T18:30:05.000000Z",
  "door_state": "DOOR_OPEN",
  "user_name": "Alice"
}
```

| 欄位 | 型別 | 說明 |
|------|------|------|
| `agent` | String | 固定為 `"FaceGuard"` |
| `timestamp` | String | ISO 8601 UTC 時間（NTP 未同步時為 `"1970-01-01T00:00:00.000000Z"`） |
| `door_state` | String | `"DOOR_OPEN"` 或 `"DOOR_CLOSED"` |
| `user_name` | String | 若門開啟前有已知使用者確認（KNOWN_CONFIRMED 窗口內，或最近 raw KNOWN 在 ~1.5s 內），填入使用者名稱；否則省略此欄位 |

### `home/security/face`

FaceVoter 輸出 `KNOWN_CONFIRMED` 時發布。

```json
{
  "agent": "FaceGuard",
  "timestamp": "2025-12-20T18:30:05.000000Z",
  "user_name": "Alice",
  "similarity": 0.95
}
```

| 欄位 | 型別 | 說明 |
|------|------|------|
| `agent` | String | 固定為 `"FaceGuard"` |
| `timestamp` | String | ISO 8601 UTC 時間 |
| `user_name` | String | 已知使用者名稱（空字串表示匿名） |
| `similarity` | Float | Cosine similarity（0.90–1.0） |

### `home/security/alert`

FaceVoter 輸出 `UNKNOWN_CONFIRMED` 時，或 RED alert 確認觸發時發布。

```json
{
  "agent": "FaceGuard",
  "timestamp": "2025-12-20T18:30:05.000000Z",
  "alert_level": "ALERT_YELLOW",
  "alert_type": "UNKNOWN_CONFIRMED"
}
```

| 欄位 | 型別 | 說明 |
|------|------|------|
| `agent` | String | 固定為 `"FaceGuard"` |
| `timestamp` | String | ISO 8601 UTC 時間 |
| `alert_level` | String | `"ALERT_RED"` 或 `"ALERT_YELLOW"` |
| `alert_type` | String | `"UNKNOWN_CONFIRMED"` |

**Yellow Alert 流程**：
- FaceGuard 發布此訊息，等待 Agent 2 回應 `home/home_state/alarm_decision`
- 若 90 秒（`ALARM_DECISION_TIMEOUT_MS`）內無回應，自動升為 RED alert（`_triggerAlarm()`）

### `home/security/status`

每 30 秒發布一次心跳狀態。

```json
{
  "agent": "FaceGuard",
  "timestamp": "2025-12-20T18:30:05.000000Z",
  "alert_level": "ALERT_RED",
  "uptime": 1800000
}
```

| 欄位 | 型別 | 說明 |
|------|------|------|
| `agent` | String | 固定為 `"FaceGuard"` |
| `timestamp` | String | ISO 8601 UTC 時間 |
| `alert_level` | String | 當前 AlertLevel |
| `uptime` | Integer | `millis()` 值（啟動後毫秒數） |

> 注意：status topic 僅包含 alert_level 與 uptime。`door_state`、`agent2_online`、`alarm_active` 等詳細狀態可透過 WebUI `/api/status` JSON API 取得，不在 MQTT status payload 中。

### `home/security/camera`

MQTT 連線時持續發布 Camera 即時畫面（best-effort，MQTT 忙碌或 Camera 未就緒時丟棄幀）。

- **發布頻率**：每 200ms（約 5fps，`CAMERA_PUB_INTERVAL_MS`）
- **Payload 格式**：原始 JPEG binary（非 JSON；無 `agent` / `timestamp` 欄位）
- **最大幀大小**：48 KB（`CAMERA_PUB_MAX_BYTES = 48 * 1024`），超出時不發布
- **傳輸方式**：PubSubClient streaming API（`beginPublish/write/endPublish`），不受 `MQTT_MAX_PACKET_SIZE` 限制
- **用途**：Agent 2 即時預覽、遠端監控；非安全事件觸發型

> **注意**：Camera topic 為持續串流，訂閱前請評估頻寬需求（約 5fps × 平均幀大小）。camera frame 本身無時間戳，訂閱端以 broker 接收時間為準。

---

## Subscribe Topics（Agent 2 → FaceGuard）

FaceGuard 訂閱以下四個主題：

| 主題 | 來源 | 說明 |
|------|------|------|
| `home/home_state/presence` | Agent 2 | 室內佔用狀態心跳 |
| `home/home_state/alarm_decision` | Agent 2 | 警報決定回應（須在 `_waitingForDecision` 期間） |
| `home/home_state/alarm_command` | Agent 2 | 主動警報指令（不受 `_waitingForDecision` 限制；驗證 `agent == AGENT2_ID` 且 timestamp < 5s） |
| `home/display/status` | Agent 2 或其他 | 顯示裝置狀態（僅記錄，不影響警戒邏輯） |

### `home/home_state/presence`

Agent 2 發布的室內佔用狀態。

```json
{
  "state": "OCCUPIED",
  "score": 3,
  "timestamp": "2025-12-20T18:30:05+08:00"
}
```

| 欄位 | 型別 | 說明 |
|------|------|------|
| `state` | String | `"OCCUPIED"` 或 `"UNOCCUPIED"` |
| `score` | Integer | 佔用置信分數（供參考） |
| `timestamp` | String | ISO 8601 |

**AlertLevel 影響**：

| state | AlertLevel |
|-------|------------|
| `"OCCUPIED"` | `ALERT_YELLOW`（如 Agent 2 在線） |
| `"UNOCCUPIED"` | `ALERT_RED` |

> **Edge case**：若 `state` 為非預期值（非 `"OCCUPIED"` 也非 `"UNOCCUPIED"`），FaceGuard 視為 `UNOCCUPIED`（`occupied=false`），但仍更新 Agent 2 在線計時（即 Agent 2 被標為在線，但警戒狀態為 `ALERT_RED`）。建議 Agent 2 確保只發送標準值。

### `home/home_state/alarm_decision`

Agent 2 回應警報決定（在 YELLOW alert 等待期間）。

```json
{
  "alarm_decision": "CANCEL_ALARM",
  "source": "button",
  "timestamp": "2025-12-20T18:30:08+08:00"
}
```

| 欄位 | 型別 | 說明 |
|------|------|------|
| `alarm_decision` | String | `"TRIGGER_ALARM"` / `"CANCEL_ALARM"` / `"NO_ACTION"` |
| `source` | String | 決定來源（由 Agent 2 定義，FaceGuard 僅記錄） |
| `timestamp` | String | ISO 8601 |

**處理邏輯**：

| alarm_decision | 行為 |
|---------------|------|
| `"TRIGGER_ALARM"` | 啟動蜂鳴器 + LED 紅色閃爍（僅在 `_waitingForDecision` 期間有效，重播訊息無效） |
| `"CANCEL_ALARM"` | 無條件清除 `_waitingForDecision` 狀態；若 alarm 已啟動則停止蜂鳴器並觸發 `onAlarmCancelled`；若 alarm 未啟動則為 no-op（不觸發 callback） |
| `"NO_ACTION"` | 不改變任何狀態 |

> **已知風險 1**：保留的 `CANCEL_ALARM` 訊息（Broker 重送）可能在新警報開始後誤取消。目前協定層未加時間戳驗證，屬設計取捨。  
> **已知風險 2**：`CANCEL_ALARM` 到達時若 Yellow alert 尚在等待中（`_waitingForDecision=true` 但 alarm 尚未啟動），會清除等待狀態，使 90 秒逾時升級機制失效。

### `home/home_state/alarm_command`

Agent 2 主動發出的警報指令（不限於等待回應期間）。

```json
{
  "agent": "epaper-home-display",
  "alarm_decision": "CANCEL_ALARM",
  "timestamp": "2025-12-20T18:30:08Z"
}
```

| 欄位 | 型別 | 說明 |
|------|------|------|
| `agent` | String | 必須為 `"epaper-home-display"`，否則忽略 |
| `alarm_decision` | String | `"TRIGGER_ALARM"` / `"CANCEL_ALARM"`；`"NO_ACTION"` 不觸發任何行為 |
| `timestamp` | String | ISO 8601；FaceGuard 驗證時間戳新鮮度（5 秒內），拒絕過期或重播訊息 |

**與 alarm_decision 的差異**：

| 項目 | `alarm_decision` | `alarm_command` |
|------|-----------------|-----------------|
| 生效條件 | 僅在 `_waitingForDecision` 期間 | 任何時刻均有效（無狀態門控） |
| 防重播 | 無（協定層已知風險） | 有（timestamp 新鮮度驗證 ≤ 5s） |
| 寄件人驗證 | 無 | 必須為 `AGENT2_ID`（`"epaper-home-display"`） |

> **用途**：允許 Agent 2 在非 Yellow-wait 期間主動觸發或取消警報，例如使用者按下實體按鈕、遠端控制等場景。`TRIGGER_ALARM` 無論 alarm 是否已啟動均呼叫 `_triggerAlarm()`；`CANCEL_ALARM` 無論 alarm 是否啟動均呼叫 `_cancelAlarm()`（alarm 未啟動時為 no-op）。
>
> **NTP 未同步例外**：若 ESP32 尚未完成 NTP 同步（`time()` < 1700000000），`_cmdIsFresh()` 無法驗算時間差，會直接放行（skip freshness check）。此狀態通常只在開機初期幾秒內發生。

### `home/display/status`

Agent 2 或顯示裝置發布的狀態通知（僅記錄至 Serial，不影響警戒邏輯）。

```json
{
  "status": "online"
}
```

| 欄位 | 型別 | 說明 |
|------|------|------|
| `status` | String | 狀態字串（FaceGuard 讀取此欄位並印 Serial） |

> **格式限制**：payload 必須是有效 JSON，長度 1–512 bytes。非 JSON payload 或超出長度限制時，FaceGuard 靜默忽略（不印 Serial log）。`status` 欄位缺失時，輸出 `display/status: unknown`（使用預設值，非靜默忽略）。

---

## 訊息格式詳述

### Timestamp 格式

所有 FaceGuard **publish** 的 `timestamp` 欄位均為 **ISO 8601 UTC 字串**（由 NTP 時間生成）：

```json
"timestamp": "2025-12-20T18:30:05.000000Z"
```

NTP 未同步時（`time()` 回傳值 < 1700000000），固定輸出：

```json
"timestamp": "1970-01-01T00:00:00.000000Z"
```

> **status payload 特例**：`uptime` 欄位仍為 `millis()` 整數（啟動後毫秒數），與 `timestamp` 並存。

Agent 2 傳來的 presence / alarm_decision 訊息格式由 Agent 2 自行決定；FaceGuard 只解析 `state`、`score`、`alarm_decision` 欄位，其餘欄位不影響解析。alarm_command 訊息則額外驗證 `agent` 與 `timestamp` 欄位。

### 主題常數（`messages.h`）

```cpp
// Publish
#define MQTT_TOPIC_DOOR     "home/security/door"
#define MQTT_TOPIC_FACE     "home/security/face"
#define MQTT_TOPIC_ALERT    "home/security/alert"
#define MQTT_TOPIC_STATUS   "home/security/status"
#define MQTT_TOPIC_CAMERA   "home/security/camera"   // 5fps JPEG binary

// Subscribe
#define MQTT_TOPIC_PRESENCE       "home/home_state/presence"
#define MQTT_TOPIC_ALARM          "home/home_state/alarm_decision"
#define MQTT_TOPIC_ALARM_CMD      "home/home_state/alarm_command"  // proactive; timestamp-validated
#define MQTT_TOPIC_DISPLAY_STATUS "home/display/status"
```

---

## 離線行為

### Agent 2 離線判定

MQTT 連線斷開，或超過 `AGENT2_OFFLINE_TIMEOUT_MS`（180 秒 / 3 分鐘）未收到 presence 訊息：

```
AgentComm::setOnConnectionChange(onAgent2Connection)
  → sm.onAgent2Online(false)
  → AlertLevel 重新計算為 ALERT_RED
  → LedController::setLevel(ALERT_RED)
```

### FaceGuard 獨立運作模式

Agent 2 離線時，FaceGuard 以 `ALERT_RED` 獨立警戒：
- 偵測到 UNKNOWN_CONFIRMED → 立即觸發蜂鳴器 + LED 閃爍 + Discord
- 不等待任何 AlarmDecision
- MQTT 背景持續重連，Agent 2 上線後恢復協作模式

### MQTT 重連策略

```
mqtt_comm task (Core 0):
  若斷線且距上次重連 >= MQTT_RECONNECT_MS:
      DNS 解析 broker hostname（mDNS / A record）
      _client.connect("faceguard", user, pass)  // 有 username 時帶認證
      → 成功: subscribe presence, alarm_decision, alarm_command, display/status 四個主題
      → 失敗: 5s 後重試（5 次失敗後清 DNS 快取強制重新解析）

主 loop AgentComm::tick():
  排空事件佇列 → 觸發 onPresence / onAlarmDecision / onAlarmCommand / onConnectionChange callbacks
```

---

## Agent 2 通訊整合指南

若要開發 Agent 2，需實作以下 MQTT 行為：

### Agent 2 必須 Publish

```
主題: home/home_state/presence
頻率: 建議每 30–60 秒一次（FaceGuard 逾時 180s / 3 分鐘後判定離線）
格式:
{
  "state": "OCCUPIED" | "UNOCCUPIED",
  "score": <integer>,
  "timestamp": "<ISO 8601>"
}
```

```
主題: home/home_state/alarm_decision
觸發時機: 收到 home/security/alert 後，在 90 秒（ALARM_DECISION_TIMEOUT_MS）內回應
格式:
{
  "alarm_decision": "TRIGGER_ALARM" | "CANCEL_ALARM" | "NO_ACTION",
  "source": "<決定來源>",
  "timestamp": "<ISO 8601>"
}
```

```
主題: home/home_state/alarm_command（可選，主動指令）
觸發時機: 任何時刻（不需等待 alert）
必填欄位:
{
  "agent": "epaper-home-display",   // 必須精確匹配，否則 FaceGuard 拒絕
  "alarm_decision": "TRIGGER_ALARM" | "CANCEL_ALARM",
  "timestamp": "<ISO 8601 UTC>",    // FaceGuard 驗證：距現在 ≤ 5 秒
}
```

> alarm_command 比 alarm_decision 具備更強的安全保護（agent_id + timestamp 雙重驗證），適合用於非緊急情境的遠端操控。

### Agent 2 必須 Subscribe

```
home/security/door    — 接收門狀態事件
home/security/face    — 接收已知使用者確認事件
home/security/alert   — 接收未知訪客警報（需在 90s 內回應 alarm_decision）
home/security/status  — 接收心跳狀態
home/security/camera  — 接收 Camera 即時畫面（raw JPEG binary，5fps）
```

### 測試連線

使用 `mosquitto_pub` / `mosquitto_sub` 測試：

```bash
# 模擬 Agent 2 發布 presence
mosquitto_pub -h <broker-ip> -t home/home_state/presence \
  -m '{"state":"OCCUPIED","score":3,"timestamp":"2025-12-20T18:30:05+08:00"}'

# 監聽 FaceGuard 發布的所有事件
mosquitto_sub -h <broker-ip> -t "home/security/#" -v

# 模擬 Agent 2 取消警報
mosquitto_pub -h <broker-ip> -t home/home_state/alarm_decision \
  -m '{"alarm_decision":"CANCEL_ALARM","source":"test","timestamp":"2025-12-20T18:31:00+08:00"}'
```
