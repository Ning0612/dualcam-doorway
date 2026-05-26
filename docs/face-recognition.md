# 人臉辨識說明

## 目錄

- [演算法設計原則](#演算法設計原則)
- [完整辨識流程](#完整辨識流程)
- [特徵萃取詳述](#特徵萃取詳述)
- [Texture Validation](#texture-validation)
- [Cosine Similarity 比對](#cosine-similarity-比對)
- [FaceVoter 時間窗投票](#facevoter-時間窗投票)
- [人臉註冊流程](#人臉註冊流程)
- [NVS 資料格式](#nvs-資料格式)
- [參數調校指南](#參數調校指南)
- [限制與已知問題](#限制與已知問題)

---

## 演算法設計原則

本系統使用**輕量 block-feature 演算法**進行人臉辨識，設計原則：

1. **不使用大型 CNN**：ESP32 資源有限，無法執行 MobileNet 等模型
2. **只處理 YUV422 Y channel**：亮度資訊足以區分人臉，不需 RGB 計算
3. **中央 60% ROI**：過濾邊緣雜訊（門框、背景），聚焦臉部區域
4. **4×4 block 統計特徵**：快速計算，對輕微光線變化具備一定魯棒性
5. **Texture validation**：過濾天花板、牆壁等非臉部場景

---

## 完整辨識流程

```
Camera frame（YUV422，QQVGA 160×120）
  │
  ▼
frame 格式驗證（非 JPEG、有效解析度、PSRAM 存在）
  │
  ▼
擷取中央 60% ROI
（X: 20–140px, Y: 18–102px）
  │
  ▼
切成 4×4 blocks = 16 個 block
每個 block 計算：
  - mean luminance（平均亮度）
  - standard deviation（標準差）
→ 32 維 raw feature vector
  │
  ▼
計算 mean block-stddev（texture score）
  │
  ├─ texture < FACE_TEXTURE_MIN_STDDEV（20.0）？
  │     → 回傳 NO_FACE（拒絕低紋理場景）
  │
  ▼
L2 normalization（將 vector 正規化為單位長度）
  │
  ▼
是否有已註冊人臉？
  │
  ├─ 無（_n == 0）→ 回傳 NO_FACE
  │
  ▼
對所有已註冊人臉計算 cosine similarity
取最高 similarity 的 match
  │
  ├─ similarity ≥ FACE_SIMILARITY_THRESHOLD（0.92）？
  │     → 回傳 KNOWN，更新 _lastMatchIdx、_lastSim
  │
  └─ similarity < 0.92 → 回傳 UNKNOWN
```

---

## 特徵萃取詳述

### ROI 擷取

```
YUV422 frame（160×120）

全圖:  W=160, H=120
ROI:   x0=32, y0=18, x1=128, y1=102
       → ROI 寬=96px, 高=84px（約 60% 中央區域）
```

### YUV422 Y channel 存取

YUV422 每 2 像素佔 4 bytes（YUYV 格式）：

```
byte[0] = Y0（第一像素亮度）
byte[1] = U
byte[2] = Y1（第二像素亮度）
byte[3] = V
```

Y channel 存取：
```cpp
uint8_t y = fb->buf[ (row * fb->width + col) * 2 ];
```

### 4×4 Block 分割

ROI 均分為 4×4 = 16 個 block：

```
每 block 寬 = ROI_W / 4 = 24px
每 block 高 = ROI_H / 4 = 21px（84/4）
```

每個 block 計算：
- **mean**：block 內所有 Y 值平均
- **stddev**：block 內 Y 值標準差

→ 每個 block 輸出 2 個 float → 16 blocks × 2 = **32 維 feature vector**

### L2 Normalization

```cpp
float norm = 0;
for (int i = 0; i < 32; i++) norm += out[i] * out[i];
norm = sqrt(norm);
if (norm > 1e-6f) for (int i = 0; i < 32; i++) out[i] /= norm;
```

正規化後，cosine similarity = dot product（因為兩向量都是單位長度）。

---

## Texture Validation

### 目的

過濾天花板、白牆、均勻背景等低紋理場景，避免將非人臉影像誤判為人臉。

### 計算方式

```cpp
float textureScore = 0;
for (int i = 0; i < 16; i++) {
    textureScore += raw_stddev[i];  // 正規化前的 stddev
}
textureScore /= 16;  // mean block-stddev
```

### 判斷

```
textureScore < FACE_TEXTURE_MIN_STDDEV（20.0）→ 回傳 NO_FACE
```

典型值參考：

| 場景 | texture score 範例 |
|------|-------------------|
| 白牆 / 天花板 | 1–8 |
| 暗處無特徵 | 2–10 |
| 正常人臉 | 25–60 |
| 高對比場景 | 50–80 |

調校：若在光線不足環境下 texture 過低，可降低 `FACE_TEXTURE_MIN_STDDEV`（最低建議 10.0）。

---

## Cosine Similarity 比對

### 計算

```cpp
float _similarity(const float* a, const float* b) {
    float dot = 0;
    for (int i = 0; i < 32; i++) dot += a[i] * b[i];
    return dot;  // 已正規化，dot product = cosine similarity
}
```

### 比對策略

```cpp
float bestSim = -1;
int   bestIdx = -1;
for (int i = 0; i < _n; i++) {
    float s = _similarity(feat, _bank[i]);
    if (s > bestSim) { bestSim = s; bestIdx = i; }
}
if (bestSim >= FACE_SIMILARITY_THRESHOLD) {
    → KNOWN（_lastMatchIdx = bestIdx, _lastSim = bestSim）
} else {
    → UNKNOWN
}
```

Cosine similarity 值域：-1 到 1，通常人臉比對結果在 0.7–1.0 之間。

---

## FaceVoter 時間窗投票

所有辨識結果必須通過 FaceVoter 才能觸發事件（詳見 [架構文件](architecture.md#facevoter-時間窗投票)）：

- **KNOWN_CONFIRMED**：8 秒內 3 次 KNOWN → 確認（防閃爍誤判）
- **UNKNOWN_CONFIRMED**：持續 10 秒且 ≥ 10 次 UNKNOWN → 確認（防短暫遮擋）
- **Idle Reset**：5 秒無臉 → 重置所有計數器

---

## 人臉註冊流程

### 透過 WebUI

1. 前往 `/face/register`
2. 輸入名稱（最多 16 字元）
3. 點擊「開始註冊」
4. 在 Camera 前方站好（確保臉部在畫面中央）
5. 系統顯示「已排程」，10 秒內偵測到的第一張臉將被儲存
6. 成功後顯示已註冊人數

### 透過 Serial

```
# 排程 enroll（匿名）
e

# 排程 enroll（帶名稱需透過 API，Serial 只能匿名）
```

### 透過 REST API

```bash
curl -b "sid=<session-token>" \
     -X POST http://agent1.local/api/face/enroll \
     -H "Content-Type: application/json" \
     -d '{"name":"Alice"}'
```

### 註冊品質建議

| 條件 | 建議 |
|------|------|
| 光線 | 均勻正面照明，避免逆光 |
| 距離 | 30–80 cm（臉部佔畫面 20–60%） |
| 角度 | 正面（<30° 偏轉） |
| 表情 | 中性表情，閉上嘴 |
| 重複 | 多次不同光線條件下重複註冊（清除後重新全部註冊） |

### 名稱限制

- 最多 16 字元（`MAX_NAME_LEN`）
- 超過部分截斷，不報錯
- 空名稱或 `nullptr` → 儲存為空字串（匿名）
- 名稱隨特徵向量一起儲存在 NVS

---

## NVS 資料格式

### 儲存位置

| NVS Key | 型別 | 說明 |
|---------|------|------|
| `face_feat` | Blob | `MAX_FACES × FEATURE_DIM × 4 bytes`（float32 陣列） |
| `face_cnt` | UInt8 | 已註冊人臉數量 |
| `face_names` | Blob | `MAX_FACES × (MAX_NAME_LEN+1) bytes`（字元陣列） |

### 容量計算

```
7 faces × 32 floats × 4 bytes = 896 bytes（特徵向量）
7 faces × 17 chars             = 119 bytes（名稱）
```

NVS 總使用約 1KB（遠低於 ESP32 NVS 限制）。

### 清除人臉資料

```cpp
FaceRecognizer::clearAll()
// → 清除 NVS face_feat, face_cnt, face_names
// → 呼叫 _onClearCb (faceVoter.reset())
```

---

## 參數調校指南

所有參數定義於 `include/config.h`：

### `FACE_SIMILARITY_THRESHOLD`（預設 0.92）

| 值 | 效果 |
|----|------|
| 0.95+ | 嚴格，減少誤認，可能增加漏認（UNKNOWN 誤判） |
| 0.92 | 平衡（建議值） |
| 0.88– | 寬鬆，提高辨識率，增加誤認風險 |

調整建議：若同一使用者在不同光線下經常 UNKNOWN，可降至 0.88–0.90。

### `FACE_TEXTURE_MIN_STDDEV`（預設 20.0）

| 值 | 效果 |
|----|------|
| 25+ | 嚴格，過濾更多低紋理場景（可能誤拒正常人臉） |
| 20 | 平衡（建議值） |
| 10– | 寬鬆，在暗處仍可辨識，但可能將背景誤判為人臉 |

調整建議：燈光昏暗環境下可降至 12–15。

### `FACE_VOTE_WINDOW_MS`（預設 10000ms）

UNKNOWN_CONFIRMED 所需最短持續時間。縮短可加速告警，增加誤觸風險。

### `FACE_VOTE_KNOWN_MIN`（預設 3）

KNOWN_CONFIRMED 所需 hit 數。增加可提高準確率，降低響應速度。

### `FACE_VOTE_KNOWN_WINDOW_MS`（預設 8000ms）

KNOWN hit 累積的時間窗口。若拍攝間隔長（500ms × 3 = 1.5s），此值通常足夠。

### `CAMERA_DETECT_INTERVAL_MS`（預設 500ms）

人臉偵測頻率。降低可提升響應速度，增加 CPU 負載。建議不低於 200ms。

---

## 限制與已知問題

| 限制 | 說明 |
|------|------|
| 最多 7 位使用者 | `MAX_FACES = 7`，可修改但增加 NVS 與比對時間 |
| 非深度學習 | 對光線、角度、遮擋較敏感，無法與 face recognition 模型媲美 |
| 不含活體偵測 | 可能被照片欺騙（此場景下不屬主要威脅） |
| 僅支援 YUV422 | JPEG 模式下的 frame 無法辨識（回傳 NO_FACE） |
| PSRAM 必須存在 | 無 PSRAM 則 YUV422 frame 配置失敗，camera 降級或失效 |
| 單 match 策略 | 取最高 similarity，不驗證閾值 gap（可能近似人臉間誤判） |
