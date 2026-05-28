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
    │                               │                           │
    │◄─ home/home_state/presence ───◄────────────────────────── │
    │◄─ home/home_state/alarm_decision ─◄───────────────────────│
```

---

## Broker 設定

MQTT Broker 資訊儲存於 NVS，透過 WebUI `/settings` 設定：

| 設定項目 | NVS Key | 預設值 |
|---------|---------|-------|
| Broker IP/hostname | `mqtt_broker` | 空（未設定） |
| Port | `mqtt_port` | 1883 |

若 Broker 未設定（空字串），`AgentComm::begin()` 為 no-op，FaceGuard 以 ALERT_RED 獨立運作。

---

## 連線規格

| 項目 | 值 |
|------|-----|
| Client ID | `faceguard` |
| QoS | 0 |
| Keep-alive | 60 秒 |
| 重連間隔 | 5 秒（`MQTT_RECONNECT_MS`） |
| Agent 2 離線判定 | 15 秒無 presence 訊息（`AGENT2_OFFLINE_TIMEOUT_MS`） |

---

## Publish Topics（FaceGuard → Agent 2）

### `home/security/door`

門狀態發生確認轉換時發布。

```json
{
  "door_state": "DOOR_OPEN",
  "user_name": "Alice",
  "timestamp": 120345
}
```

| 欄位 | 型別 | 說明 |
|------|------|------|
| `door_state` | String | `"DOOR_OPEN"` 或 `"DOOR_CLOSED"` |
| `user_name` | String | 若 KNOWN_CONFIRMED 先於門開啟，填入使用者名稱；否則省略此欄位 |
| `timestamp` | Integer | `millis()` 值（啟動後毫秒數） |

### `home/security/face`

FaceVoter 輸出 `KNOWN_CONFIRMED` 時發布。

```json
{
  "user_name": "Alice",
  "similarity": 0.95,
  "timestamp": 120000
}
```

| 欄位 | 型別 | 說明 |
|------|------|------|
| `user_name` | String | 已知使用者名稱（空字串表示匿名） |
| `similarity` | Float | Cosine similarity（0.92–1.0） |
| `timestamp` | Integer | `millis()` 值 |

### `home/security/alert`

FaceVoter 輸出 `UNKNOWN_CONFIRMED` 時，或 RED alert 確認觸發時發布。

```json
{
  "alert_level": "ALERT_YELLOW",
  "alert_type": "UNKNOWN_CONFIRMED",
  "timestamp": 125000
}
```

| 欄位 | 型別 | 說明 |
|------|------|------|
| `alert_level` | String | `"ALERT_RED"` 或 `"ALERT_YELLOW"` |
| `alert_type` | String | `"UNKNOWN_CONFIRMED"` |
| `timestamp` | Integer | `millis()` 值 |

**Yellow Alert 流程**：
- FaceGuard 發布此訊息，等待 Agent 2 回應 `home/home_state/alarm_decision`
- 若 30 秒內無回應，自動升為 RED alert（`_triggerAlarm()`）

### `home/security/status`

每 30 秒發布一次心跳狀態。

```json
{
  "alert_level": "ALERT_RED",
  "uptime": 1800000,
  "timestamp": 1800000
}
```

| 欄位 | 型別 | 說明 |
|------|------|------|
| `alert_level` | String | 當前 AlertLevel |
| `uptime` | Integer | `millis()` 值（啟動後毫秒數） |
| `timestamp` | Integer | `millis()` 值 |

> 注意：status topic 僅包含 alert_level 與 uptime。`door_state`、`agent2_online`、`alarm_active` 等詳細狀態可透過 WebUI `/api/status` JSON API 取得，不在 MQTT status payload 中。

---

## Subscribe Topics（Agent 2 → FaceGuard）

### `home/home_state/presence`

Agent 2 發布的室內佔用狀態。

```json
{
  "presence_state": "OCCUPIED",
  "presence_score": 3,
  "timestamp": "2025-12-20T18:30:05+08:00"
}
```

| 欄位 | 型別 | 說明 |
|------|------|------|
| `presence_state` | String | `"OCCUPIED"` 或 `"UNOCCUPIED"` |
| `presence_score` | Integer | 佔用置信分數（供參考） |
| `timestamp` | String | ISO 8601 |

**AlertLevel 影響**：

| presence_state | AlertLevel |
|---------------|------------|
| `"OCCUPIED"` | `ALERT_YELLOW`（如 Agent 2 在線） |
| `"UNOCCUPIED"` | `ALERT_RED` |

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
| `"TRIGGER_ALARM"` | 啟動蜂鳴器 + LED 紅色閃爍（僅在 `_waitingForDecision` 期間有效） |
| `"CANCEL_ALARM"` | 停止蜂鳴器，關閉閃爍，LED 恢復 AlertLevel 顏色 |
| `"NO_ACTION"` | 不改變任何狀態 |

> **已知風險**：保留的 CANCEL_ALARM 訊息（Broker 重送）可能在新警報開始後誤取消。目前協定層未加時間戳驗證，屬設計取捨。

---

## 訊息格式詳述

### Timestamp 格式

所有 MQTT publish 的 `timestamp` 欄位均為 `millis()` 整數值（啟動後毫秒數），**非 ISO 8601 字串**。

```json
"timestamp": 1800000
```

ISO 8601 timestamp 僅用於 LogManager 日誌（透過 WebUI 查詢），不出現在 MQTT payload 中。NTP 未同步時，日誌以 `{"time_synced": false}` 標記。Agent 2 傳來的 presence / alarm_decision 訊息格式由 Agent 2 自行決定；FaceGuard 只解析 `presence_state`、`presence_score`、`alarm_decision` 欄位，其餘欄位（包含 Agent 2 的 timestamp 格式）不影響解析。

### 主題常數（`messages.h`）

```cpp
// Publish
#define MQTT_TOPIC_DOOR     "home/security/door"
#define MQTT_TOPIC_FACE     "home/security/face"
#define MQTT_TOPIC_ALERT    "home/security/alert"
#define MQTT_TOPIC_STATUS   "home/security/status"

// Subscribe
#define MQTT_TOPIC_PRESENCE "home/home_state/presence"
#define MQTT_TOPIC_ALARM    "home/home_state/alarm_decision"
```

---

## 離線行為

### Agent 2 離線判定

MQTT 連線斷開，或超過 `AGENT2_OFFLINE_TIMEOUT_MS`（15 秒）未收到 presence 訊息：

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
AgentComm::tick():
  if (!_client.connected()):
      if (now - _lastReconnectMs >= MQTT_RECONNECT_MS):
          _reconnect()
              → _client.connect("faceguard")
              → 成功: subscribe presence & alarm topics
              → 失敗: 5s 後重試
```

---

## Agent 2 通訊整合指南

若要開發 Agent 2，需實作以下 MQTT 行為：

### Agent 2 必須 Publish

```
主題: home/home_state/presence
頻率: 建議每 5–30 秒一次（FaceGuard 逾時 15s 後判定離線）
格式:
{
  "presence_state": "OCCUPIED" | "UNOCCUPIED",
  "presence_score": <integer>,
  "timestamp": "<ISO 8601>"
}
```

```
主題: home/home_state/alarm_decision
觸發時機: 收到 home/security/alert 後，在 30 秒內回應
格式:
{
  "alarm_decision": "TRIGGER_ALARM" | "CANCEL_ALARM" | "NO_ACTION",
  "source": "<決定來源>",
  "timestamp": "<ISO 8601>"
}
```

### Agent 2 必須 Subscribe

```
home/security/door    — 接收門狀態事件
home/security/face    — 接收已知使用者確認事件
home/security/alert   — 接收未知訪客警報（需在 30s 內回應）
home/security/status  — 接收心跳狀態
```

### 測試連線

使用 `mosquitto_pub` / `mosquitto_sub` 測試：

```bash
# 模擬 Agent 2 發布 presence
mosquitto_pub -h <broker-ip> -t home/home_state/presence \
  -m '{"presence_state":"OCCUPIED","presence_score":3,"timestamp":"2025-12-20T18:30:05+08:00"}'

# 監聽 FaceGuard 發布的所有事件
mosquitto_sub -h <broker-ip> -t "home/security/#" -v

# 模擬 Agent 2 取消警報
mosquitto_pub -h <broker-ip> -t home/home_state/alarm_decision \
  -m '{"alarm_decision":"CANCEL_ALARM","source":"test","timestamp":"2025-12-20T18:31:00+08:00"}'
```
