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

本系統使用 **HOG-lite（輕量梯度方向直方圖）** 進行人臉辨識，設計原則：

1. **不使用大型 CNN**：ESP32 資源有限，無法執行 MobileNet 等模型
2. **只處理 YUV422 Y channel**：亮度資訊足以計算邊緣梯度，不需 RGB
3. **中央 60% ROI**：過濾邊緣雜訊（門框、背景），聚焦臉部區域
4. **4×4 cells × 4 orientation bins = 64-dim feature**：方向梯度直方圖捕捉邊緣結構
5. **Per-cell L1 + global L2 normalize**：對光照變化具備魯棒性
6. **Multi-template per user**：每位使用者最多 5 個模板，提升不同光線/角度下的辨識率

---

## 完整辨識流程

```
Camera frame（YUV422，QVGA 320×240）
  │
  ▼
frame 格式驗證（非 JPEG、有效解析度、PSRAM 存在）
  │
  ▼
擷取中央 60% ROI（W/5..W*4/5, H/5..H*4/5）
  │
  ▼
切成 4×4 grid = 16 cells
每個 cell 計算 4-bin 無符號梯度方向直方圖（HOG-lite）：
  中央差分計算每像素 dx, dy（Y channel only）：
    dx = Y(x+1) - Y(x-1), dy = Y(y+1) - Y(y-1)
  ax = |dx|, ay = |dy|, mag = ax + ay
  mag < 2（noise floor）→ 忽略
  sameSign = (dx ≥ 0) == (dy ≥ 0)
  ax >= ay → (sameSign ? bin 0 : bin 1)（水平主導邊緣）
  ay >  ax → (sameSign ? bin 2 : bin 3)（垂直主導邊緣）
→ 16 cells × 4 bins = 64 維 raw feature vector
  │
  ▼
Per-cell L1 normalization（每 cell 梯度除以該 cell 總量）
累計 totalEnergy / totalPix（texture score 原始值）
統計 activeCells（cellEnergy/cellPix > 0.5f 的 cell 數）
  │
  ├─ active cells < 4 → _extract() 回傳 0.f → NO_FACE
  │
  ▼
Global L2 normalization → 64-float 單位向量
  │
  ▼
Texture gate（在 recognize() 中）：
  texture = totalEnergy / totalPix
  ├─ texture < FACE_TEXTURE_MIN_STDDEV（12.0）？→ 回傳 NO_FACE
  │
  ▼
是否有已註冊人臉？（_n == 0 → 回傳 NO_FACE）
  │
  ▼
Per-user best-template cosine similarity
（每位使用者最多 MAX_TEMPLATES_PER_USER = 5 個模板，取最高 similarity）
取最高 similarity user（bestSim）與第二高 user（secondSim）
  │
  ├─ 使用者 ≥ 2 人且 bestSim - secondSim < FACE_MARGIN_MIN（0.03）？
  │     → 回傳 UNKNOWN（無足夠分差，可能混淆）
  │
  ▼
bestSim ≥ FACE_SIMILARITY_THRESHOLD（0.90）？
  ├─ 是 → 回傳 KNOWN（更新 _lastMatchName、_lastSim、_lastTex）
  └─ 否 → 回傳 UNKNOWN
```

---

## 特徵萃取詳述

### ROI 擷取

```
YUV422 frame（QVGA 320×240）

全圖:  W=320, H=240
ROI:   x0 = W/5 = 64,    x1 = W*4/5 = 256
       y0 = H/5 = 48,    y1 = H*4/5 = 192
       → ROI 寬=192px, 高=144px（中央 60%）
```

### YUV422 Y channel 存取

YUV422（YUYV 格式）每 2 像素佔 4 bytes：

```
byte[0] = Y0（第一像素亮度）
byte[1] = U
byte[2] = Y1（第二像素亮度）
byte[3] = V
```

Y channel 存取（pixel index 0-based）：

```cpp
uint8_t y = fb->buf[ pixel_index * 2 ];
```

### HOG-lite：4×4 cells × 4-bin 方向直方圖

ROI 均分為 4×4 = 16 個 cell，每個 cell 對所有像素計算中央差分梯度：

```cpp
// 中央差分（Y channel，1px 半徑）
int16_t dx = Y(x+1) - Y(x-1);   // 水平梯度
int16_t dy = Y(y+1) - Y(y-1);   // 垂直梯度（rowNext - rowPrev）
int16_t ax = abs(dx), ay = abs(dy);
float   mag = (float)(ax + ay);

if (mag < 2.f) continue;  // noise floor，忽略近零梯度

// 無符號方向 bin：依 dx/dy 同號/異號 + 主導方向決定
bool sameSign = (dx >= 0) == (dy >= 0);
int  bin      = (ax >= ay) ? (sameSign ? 0 : 1)  // 水平主導
                           : (sameSign ? 2 : 3);  // 垂直主導
cell_hist[bin] += mag;
```

Bin 語意：0 = 水平同向、1 = 水平異向、2 = 垂直同向、3 = 垂直異向

→ 16 cells × 4 bins = **64 維 raw feature vector**

### 正規化流程

```
1. Per-cell L1 normalize（每個 cell 的 4-bin 除以該 cell 梯度總量）
2. Global L2 normalize（整個 64-dim vector 除以 L2 norm）
→ 輸出：64-float 單位向量
```

雙重正規化使特徵對整體亮度變化和局部對比度差異具備魯棒性。

---

## Texture Validation

### 目的

過濾天花板、白牆、均勻背景等低梯度場景，避免將非人臉影像送入比對。

### 計算方式

```cpp
float textureScore = total_L1_gradient / total_valid_pixels;  // mean L1 gradient per pixel
```

`total_L1_gradient` 只累計 mag ≥ 2.f 的像素，邊界像素（x=0, x=W-1）排除。

### 判斷（兩關）

**關 1：Texture gate**
```
textureScore < FACE_TEXTURE_MIN_STDDEV（12.0）→ 回傳 NO_FACE
```

**關 2：Active-cell gate**
- 每個 cell：cellEnergy / cellPix > 0.5f → 視為 active
- 16 個 cell 中 active 數 < 4 → 回傳 NO_FACE（防止單條邊緣線或陰影條紋通過）

典型值參考：

| 場景 | texture score 範例 |
|------|-------------------|
| 白牆 / 天花板 | 2–6 |
| 暗處無特徵 | 1–8 |
| 正常人臉（中等光線） | 15–40 |
| 高對比場景 | 35–60 |

調校：光線不足環境下可降低 `FACE_TEXTURE_MIN_STDDEV`（最低建議 8.0）。

---

## Cosine Similarity 比對

### 原理

特徵向量經 L2 normalize 後為單位向量，cosine similarity = 點積：

```cpp
float dot = 0;
for (int i = 0; i < FEATURE_DIM; i++) dot += a[i] * b[i];
return dot;  // 值域 [-1, 1]，人臉比對通常在 0.75–1.0
```

### Multi-template 比對策略

每位使用者可有多個模板（最多 `MAX_TEMPLATES_PER_USER = 5`），取最佳模板 similarity 代表該使用者：

```cpp
float bestSim = -1, secondSim = -1;
int   bestUser = -1;

for (int u = 0; u < _n; u++) {
    float userBest = -1;
    for (int t = 0; t < _tcnt[u]; t++) {
        float s = cosineSim(feat, _bank[u][t]);
        if (s > userBest) userBest = s;
    }
    if (userBest > bestSim) {
        secondSim = bestSim;
        bestSim = userBest; bestUser = u;
    } else if (userBest > secondSim) {
        secondSim = userBest;
    }
}
```

### Margin Check（≥ 2 位使用者時）

```
if (bestSim - secondSim < FACE_MARGIN_MIN) → UNKNOWN（防近似人臉誤判）
```

`FACE_MARGIN_MIN = 0.03`：最高分與次高分必須有 0.03 以上的差距，才確認為 KNOWN。

### 最終判斷

```
bestSim ≥ FACE_SIMILARITY_THRESHOLD（0.90）且 margin 足夠 → FACE_KNOWN
否則 → FACE_UNKNOWN
```

---

## FaceVoter 時間窗投票

所有辨識結果必須通過 FaceVoter 才能觸發事件：

- **KNOWN_CONFIRMED**：`FACE_VOTE_KNOWN_WINDOW_MS`（8 s）內累積 `FACE_VOTE_KNOWN_MIN`（3）次 KNOWN
  - Confirmed 後記錄 `confirmedName`；同一張臉連續在畫面中不重複觸發
- **UNKNOWN_CONFIRMED**：持續偵測 UNKNOWN 超過 `FACE_VOTE_WINDOW_MS`（10 s）且至少 `FACE_VOTE_UNKNOWN_MIN_HITS`（10）次
  - 偶發 KNOWN raw result 不重置 unknown timer
  - Unknown 持續存在時，每隔 `FACE_VOTE_WINDOW_MS` 可重新觸發
- **Idle Reset**：`FACE_VOTE_IDLE_MS`（5 s）無臉 → 重置所有計數器與 confirmedName

---

## 人臉註冊流程

### 透過 WebUI

1. 前往 `/dashboard`，點擊人臉管理區的「新增」
2. 輸入名稱（最多 16 字元，僅保留可列印 ASCII 32–126，超出截斷）
3. 點擊「開始註冊」（觸發 `POST /api/face/enroll`，回應 `{"scheduled":true}`）
4. 在 Camera 前方站好（確保臉部在畫面中央）
5. **排程後**於下一次偵測到有效 YUV422 face frame 時擷取特徵並儲存（不做 texture 門檻驗證）
6. `CAMERA_ENROLL_TIMEOUT_MS`（10 s）內未偵測到臉則排程取消
7. **同一使用者可多次 enroll**（追加模板，不覆蓋），最多 5 個模板；空名稱以匿名儲存

### 透過 REST API

```bash
# 需要先取得 CSRF token（從 /dashboard 頁面取得）
curl -b "sid=<session-token>" \
     -X POST http://faceguard.local/api/face/enroll \
     -H "Content-Type: application/x-www-form-urlencoded" \
     -d "csrf=<token>&name=Alice"
```

### 多模板策略

```
同名使用者第一次 enroll  → 建立使用者，template count = 1
同名使用者第二次 enroll  → 追加模板，template count = 2
...
同名使用者第五次 enroll  → 追加模板，template count = 5
同名使用者第六次 enroll  → 回傳失敗（超過上限）
```

建議：在不同光線、角度下各 enroll 2–3 次，以提升辨識率。

### 名稱限制

- 最多 16 字元（`MAX_NAME_LEN`）
- 超過部分截斷，不報錯
- 空名稱或 `nullptr` → 儲存為空字串（匿名）
- 名稱隨特徵向量一起儲存在 NVS

---

## NVS 資料格式

### 儲存位置

所有 key 使用 namespace `"agent_cfg"`：

| NVS Key | 型別 | 大小 | 說明 |
|---------|------|------|------|
| `face_feat` | Blob | 最大 8960 bytes | float32 特徵向量；實際 = enrolled × MAX_TEMPLATES × FEATURE_DIM × 4 |
| `face_cnt` | UInt8 | 1 byte | 已註冊使用者數量（0 = 無） |
| `face_names` | Blob | 119 bytes | `MAX_FACES × (MAX_NAME_LEN+1)` = 7×17 bytes |
| `face_tcnt` | Blob | 7 bytes | 各使用者的模板數量，`MAX_FACES` bytes |

### 容量計算

```
最大容量：7 users × 5 templates × 64 floats × 4 bytes = 8960 bytes（特徵向量）
名稱：     7 users × 17 chars                           = 119  bytes
模板計數： 7 bytes
NVS 總計：約 9.1 KB（遠低於 ESP32 NVS partition 限制）
```

### 清除人臉資料

透過 WebUI（`POST /api/face/clear`）：

```
clearAll() 效果：
  face_cnt 寫入 0
  face_feat、face_names、face_tcnt blob 從 NVS 移除
→ _n = 0，所有比對回傳 NO_FACE，直到重新 enroll
```

> **版本相容性**：若 NVS 中 `face_feat` 大小與目前常數不符（如從單模板版本升級），
> 系統啟動時 `_load()` 自動偵測 size mismatch，設 `_n=0` 並呼叫 `_persist()` 清空 NVS blob，
> 效果等同清除；但不觸發 `clearAll()` 的 callback（`_onClearCb`），需重新 enroll 所有使用者。

---

## 參數調校指南

所有參數定義於 `include/config.h`：

### `FACE_SIMILARITY_THRESHOLD`（預設 0.90）

| 值 | 效果 |
|----|------|
| 0.95+ | 嚴格，減少誤認，可能增加漏認（UNKNOWN 誤判） |
| 0.90 | 平衡（預設值） |
| 0.85– | 寬鬆，提高辨識率，增加誤認風險 |

調整建議：若同一使用者在不同光線下經常 UNKNOWN，可降至 0.87–0.89；若有誤認，提高至 0.92。

### `FACE_MARGIN_MIN`（預設 0.03）

最高分與次高分的最小差距（≥ 2 位使用者時啟用）。

| 值 | 效果 |
|----|------|
| 0.05+ | 嚴格，外貌相似的使用者之間不易混淆，但 UNKNOWN 率增加 |
| 0.03 | 平衡（預設值） |
| 0.01– | 寬鬆，容易在相似使用者間誤認 |

### `FACE_TEXTURE_MIN_STDDEV`（預設 12.0）

| 值 | 效果 |
|----|------|
| 18+ | 嚴格，過濾更多低梯度場景（昏暗環境下可能拒絕正常人臉） |
| 12 | 平衡（預設值） |
| 6– | 寬鬆，在暗處仍可辨識，但可能將背景誤判為人臉 |

### `FACE_VOTE_WINDOW_MS`（預設 10000ms）

UNKNOWN_CONFIRMED 所需最短持續時間。縮短可加速告警，增加誤觸風險。

### `FACE_VOTE_KNOWN_MIN`（預設 3）

KNOWN_CONFIRMED 所需 hit 數。增加可提高準確率，降低響應速度。

### `FACE_VOTE_KNOWN_WINDOW_MS`（預設 8000ms）

KNOWN hit 累積的時間窗口。偵測間隔 500ms × 3 hits = 最短 1.5s，此值通常足夠。

### `CAMERA_DETECT_INTERVAL_MS`（預設 500ms）

人臉偵測頻率。降低可提升響應速度，增加 CPU 負載。建議不低於 200ms。

---

## 限制與已知問題

| 限制 | 說明 |
|------|------|
| 最多 7 位使用者 | `MAX_FACES = 7`，可修改但增加 NVS 使用量與比對時間 |
| 非深度學習 | HOG-lite 對大角度偏轉（>45°）、強逆光較敏感，無法與 CNN 模型媲美 |
| 不含活體偵測 | 可能被照片欺騙（此部署場景下風險較低） |
| 僅支援 YUV422 | JPEG 模式下的 frame 無法辨識（回傳 NO_FACE） |
| PSRAM 必須存在 | 無 PSRAM 則 YUV422 frame 配置失敗，camera 降級或失效 |
| 多模板上限 | 每位使用者最多 5 個模板，超過後需先 clear 再重新 enroll |
