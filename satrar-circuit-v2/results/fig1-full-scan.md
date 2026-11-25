# V2 全電路替代候選掃描

輸入 `fig1.aag`。共 9 個 AIG AND、18 個接收腳、36 個 pin fault。
每個接收腳查 sa0 與 sa1。每筆候選用 V2 的兩階段 CEC 驗證，每階段 timeout 為 10 秒。
沿用 CLI 預設的接入範圍，包含目標直接接收 gate。

## 各模式統計

| 模式 | #wire | #tar 有通過候選的接收腳 | #alt 通過 CEC 的候選 | 搜尋回傳 | CEC 拒絕 | CEC 未判定 | 達試探上限的 fault |
|---|---:|---:|---:|---:|---:|---:|---:|
| l0 | 18 | 18 | 24 | 24 | 0 | 0 | 0 |
| l1 | 18 | 18 | 29 | 29 | 0 | 0 | 0 |
| l2 | 18 | 18 | 33 | 33 | 0 | 0 | 0 |
| wire | 18 | 18 | 33 | 33 | 0 | 0 | 0 |
| gate | 18 | 18 | 108 | 108 | 0 | 0 | 0 |
| gate_nolearn | 18 | 18 | 120 | 120 | 0 | 0 | 0 |
| gate_nofilter | 18 | 18 | 141 | 141 | 0 | 0 | 0 |
| sub | 18 | 18 | 135 | 135 | 0 | 0 | 0 |

同一接收腳的 sa0／sa1 在 #tar 合計一次。#alt 以帶有 fault、接入點與帶極性表達式的 candidate key 計數。
合計 623 筆模式輸出通過 CEC，跨模式去重後為 182 筆。

## 排除直接接收 gate 後的統計

這張表對已驗證候選做事後篩選，僅保留接入 gate 與目標 gate 不同的 keys。
搜尋時仍包含直接接收 gate，所以這些計數僅顯示接入範圍的影響。完整論文口徑需要在搜尋前限制接入點，並單獨量測搜尋時間。

| 模式 | #tar | #alt |
|---|---:|---:|
| l0 | 2 | 2 |
| l1 | 5 | 6 |
| l2 | 6 | 10 |
| wire | 6 | 10 |
| gate | 10 | 70 |
| gate_nolearn | 10 | 82 |
| gate_nofilter | 10 | 101 |
| sub | 10 | 93 |

## 每個輸入腳與 stuck-at 值

每一列的各模式欄位是 CEC 通過筆數。* 表示該模式的試探達上限。
Exact SAT 表示原電路可偵測該 fault。UNSAT 且標為冗餘表示原本已可把該腳固定為指定常數。UNKNOWN 表示未判定。

| AIG var | pin | 來源 raw literal | fault | Exact | l0 | l1 | l2 | wire | gate | gate_nolearn | gate_nofilter | sub |
|---:|---:|---:|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 7 | 0 | 8 | sa0 | SAT | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 7 | 0 | 8 | sa1 | SAT | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
| 7 | 1 | 4 | sa0 | SAT | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 7 | 1 | 4 | sa1 | SAT | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
| 8 | 0 | 10 | sa0 | SAT | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 8 | 0 | 10 | sa1 | SAT | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
| 8 | 1 | 7 | sa0 | SAT | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 8 | 1 | 7 | sa1 | SAT | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
| 9 | 0 | 4 | sa0 | SAT | 0 | 0 | 0 | 0 | 1 | 1 | 1 | 2 |
| 9 | 0 | 4 | sa1 | SAT | 2 | 3 | 3 | 3 | 4 | 5 | 4 | 4 |
| 9 | 1 | 2 | sa0 | SAT | 0 | 0 | 0 | 0 | 1 | 1 | 1 | 2 |
| 9 | 1 | 2 | sa1 | SAT | 2 | 2 | 2 | 2 | 3 | 3 | 3 | 3 |
| 10 | 0 | 14 | sa0 | SAT | 0 | 0 | 0 | 0 | 4 | 4 | 5 | 4 |
| 10 | 0 | 14 | sa1 | SAT | 2 | 2 | 3 | 3 | 8 | 8 | 10 | 10 |
| 10 | 1 | 6 | sa0 | SAT | 0 | 0 | 0 | 0 | 2 | 2 | 4 | 2 |
| 10 | 1 | 6 | sa1 | SAT | 1 | 1 | 1 | 1 | 4 | 4 | 4 | 4 |
| 11 | 0 | 17 | sa0 | SAT | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 11 | 0 | 17 | sa1 | SAT | 1 | 1 | 1 | 1 | 2 | 2 | 2 | 2 |
| 11 | 1 | 15 | sa0 | SAT | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 11 | 1 | 15 | sa1 | SAT | 1 | 1 | 1 | 1 | 3 | 3 | 3 | 3 |
| 12 | 0 | 17 | sa0 | SAT | 0 | 1 | 2 | 2 | 11 | 15 | 15 | 15 |
| 12 | 0 | 17 | sa1 | SAT | 2 | 2 | 2 | 2 | 6 | 6 | 10 | 6 |
| 12 | 1 | 9 | sa0 | SAT | 0 | 1 | 2 | 2 | 13 | 16 | 15 | 17 |
| 12 | 1 | 9 | sa1 | SAT | 3 | 3 | 3 | 3 | 8 | 8 | 16 | 8 |
| 13 | 0 | 25 | sa0 | SAT | 0 | 0 | 0 | 0 | 1 | 1 | 2 | 2 |
| 13 | 0 | 25 | sa1 | SAT | 1 | 3 | 4 | 4 | 16 | 20 | 24 | 20 |
| 13 | 1 | 18 | sa0 | SAT | 0 | 0 | 0 | 0 | 2 | 2 | 2 | 3 |
| 13 | 1 | 18 | sa1 | SAT | 1 | 1 | 1 | 1 | 2 | 2 | 2 | 4 |
| 14 | 0 | 27 | sa0 | SAT | 0 | 0 | 0 | 0 | 1 | 1 | 1 | 3 |
| 14 | 0 | 27 | sa1 | SAT | 1 | 1 | 1 | 1 | 3 | 3 | 3 | 4 |
| 14 | 1 | 21 | sa0 | SAT | 0 | 0 | 0 | 0 | 1 | 1 | 1 | 3 |
| 14 | 1 | 21 | sa1 | SAT | 1 | 1 | 1 | 1 | 5 | 5 | 6 | 5 |
| 15 | 0 | 29 | sa0 | SAT | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 15 | 0 | 29 | sa1 | SAT | 1 | 1 | 1 | 1 | 2 | 2 | 2 | 4 |
| 15 | 1 | 12 | sa0 | SAT | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 15 | 1 | 12 | sa1 | SAT | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |

## 每個 gate 的輸出與接入候選

輸出 sa0／sa1 欄位是直接固定該 gate 輸出時的 exact 判定。
接入候選欄位按接入 gate 計算通過 CEC 的不同 keys，同一 key 跨模式只算一次。

| AIG var | 輸出 sa0 | 輸出 sa1 | 接入此 gate 的已驗證候選 |
|---:|---|---|---:|
| 7 | SAT | SAT | 2 |
| 8 | SAT | SAT | 2 |
| 9 | SAT | SAT | 2 |
| 10 | SAT | SAT | 3 |
| 11 | SAT | SAT | 5 |
| 12 | SAT | SAT | 6 |
| 13 | SAT | SAT | 25 |
| 14 | SAT | SAT | 49 |
| 15 | SAT | SAT | 88 |

## 候選明細

Key 依序為目標 var、pin、stuck 值、接入 gate var、類型和表達式。w 表示 wire，後面是接入 literal 的 raw 值。
g 表示 gate，斜線左邊是 separator 的 AND 項，右邊是可選的 OR 項。改寫接入 separator 的反相。

| Canonical key | 模式與 CEC 結果 |
|---|---|
| `7,0,1,7,w,8` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `7,1,1,7,w,4` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `8,0,1,8,w,10` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `8,1,1,8,w,7` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `9,0,0,14,g,2,4,25/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `9,0,0,14,g,2,4/8+16` | sub（accepted） |
| `9,0,1,9,w,4` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `9,0,1,13,w,4` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `9,0,1,15,w,4` | l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `9,0,1,15,g,5,21/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `9,0,1,15,g,5,15/` | gate_nolearn（accepted） |
| `9,1,0,14,g,2,4,25/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `9,1,0,14,g,2,4/8+16` | sub（accepted） |
| `9,1,1,9,w,2` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `9,1,1,13,w,2` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `9,1,1,15,g,3,21/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `10,0,0,14,g,6,14/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `10,0,0,14,g,6,23/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `10,0,0,14,g,4,6,8/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `10,0,0,14,g,4,6,25/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `10,0,0,14,g,6,17,23/` | gate_nofilter（accepted） |
| `10,0,1,10,w,14` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `10,0,1,10,w,23` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `10,0,1,15,w,23` | l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `10,0,1,15,g,15,27/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `10,0,1,15,g,22,27/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `10,0,1,15,g,15,17/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `10,0,1,15,g,6,15/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `10,0,1,15,g,15,22/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `10,0,1,15,g,6,22/` | gate_nofilter（accepted） |
| `10,0,1,15,g,17,22/` | gate_nofilter（accepted） |
| `10,0,1,15,g,27/5+9` | sub（accepted） |
| `10,0,1,15,g,15/19+24` | sub（accepted） |
| `10,1,0,14,g,6,14/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `10,1,0,14,g,6,23/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `10,1,0,14,g,4,6,8/` | gate_nofilter（accepted） |
| `10,1,0,14,g,4,6,25/` | gate_nofilter（accepted） |
| `10,1,1,10,w,6` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `10,1,1,15,g,7,27/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `10,1,1,15,g,7,19/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `10,1,1,15,g,3,7/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `11,0,1,11,w,17` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `11,0,1,11,g,7,10/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `11,1,1,11,w,15` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `11,1,1,11,g,4,8/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `11,1,1,11,g,4,25/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,0,0,13,w,23` | l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,0,0,15,w,23` | l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,0,0,13,g,9,17/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,0,0,13,g,9,22/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,0,0,13,g,15,17/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,0,0,13,g,17,22/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,0,0,15,g,21,22/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,0,0,15,g,9,17,21/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,0,0,15,g,15,17,21/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,0,0,15,g,9,22/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,0,0,15,g,9,17/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,0,0,15,g,9,21,22/` | gate_nolearn（accepted） |
| `12,0,0,15,g,15,21,22/` | gate_nolearn（accepted） |
| `12,0,0,15,g,17,21,22/` | gate_nolearn（accepted） |
| `12,0,0,15,g,17,22/` | gate_nolearn（accepted） |
| `12,0,0,13,g,15,22/` | gate_nofilter（accepted） |
| `12,0,0,15,g,15,22/` | gate_nofilter（accepted） |
| `12,0,0,15,g,15,17,18/` | gate_nofilter（accepted） |
| `12,0,0,15,g,15,17/` | gate_nofilter（accepted） |
| `12,0,0,13,g,9/6+11` | sub（accepted） |
| `12,0,0,15,g,9,21/6+11` | sub（accepted） |
| `12,0,0,15,g,15,21/6+11` | sub（accepted） |
| `12,0,0,15,g,9/6+11` | sub（accepted） |
| `12,0,1,12,w,17` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,0,1,12,w,22` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,0,1,12,g,7,10/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,0,1,14,g,16,18/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,0,1,14,g,18,23/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,0,1,14,g,7,10,18/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,0,1,14,g,2,4,16/` | gate_nofilter（accepted） |
| `12,0,1,14,g,2,4,23/` | gate_nofilter（accepted） |
| `12,0,1,14,g,9,18,23/` | gate_nofilter（accepted） |
| `12,0,1,14,g,15,18,23/` | gate_nofilter（accepted） |
| `12,1,0,13,w,23` | l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,0,15,w,23` | l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,0,13,g,9,17/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,0,13,g,15,17/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,0,13,g,17,22/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,0,13,g,9,22/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,0,15,g,21,22/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,0,15,g,15,17,21/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,0,15,g,9,17,21/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,0,15,g,9,17/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,0,15,g,15,17/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,0,15,g,17,22/` | gate（accepted）, gate_nolearn（accepted）, sub（accepted） |
| `12,1,0,15,g,9,22/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,0,15,g,17,21,22/` | gate_nolearn（accepted） |
| `12,1,0,15,g,9,21,22/` | gate_nolearn（accepted） |
| `12,1,0,15,g,15,21,22/` | gate_nolearn（accepted） |
| `12,1,0,13,g,15,22/` | gate_nofilter（accepted） |
| `12,1,0,15,g,15,22/` | gate_nofilter（accepted） |
| `12,1,0,15,g,15,17,18/` | gate_nofilter（accepted） |
| `12,1,0,13,g,9/6+11` | sub（accepted） |
| `12,1,0,15,g,9,21/6+11` | sub（accepted） |
| `12,1,0,15,g,15,21/6+11` | sub（accepted） |
| `12,1,0,15,g,9/6+11` | sub（accepted） |
| `12,1,1,12,w,9` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,1,12,w,15` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,1,12,w,22` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,1,14,g,8,18/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,1,14,g,14,18/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,1,14,g,18,23/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,1,14,g,2,8,14/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,1,14,g,2,14/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `12,1,1,14,g,2,4,8/` | gate_nofilter（accepted） |
| `12,1,1,14,g,2,4,14/` | gate_nofilter（accepted） |
| `12,1,1,14,g,2,4,23/` | gate_nofilter（accepted） |
| `12,1,1,14,g,2,17,23/` | gate_nofilter（accepted） |
| `12,1,1,14,g,2,11,23/` | gate_nofilter（accepted） |
| `12,1,1,14,g,2,18,23/` | gate_nofilter（accepted） |
| `12,1,1,14,g,17,18,23/` | gate_nofilter（accepted） |
| `12,1,1,14,g,11,18,23/` | gate_nofilter（accepted） |
| `13,0,0,14,g,18,25/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,0,0,14,g,2,4,25/` | gate_nofilter（accepted） |
| `13,0,0,14,g,18/8+16` | sub（accepted） |
| `13,0,1,13,w,25` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,0,1,13,w,23` | l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,0,1,15,w,25` | l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,0,1,15,w,23` | l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,0,1,13,g,9,17/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,0,1,13,g,15,17/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,0,1,13,g,17,22/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,0,1,13,g,9,22/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,0,1,15,g,21,24/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,0,1,15,g,21,22/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,0,1,15,g,15,17,21/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,0,1,15,g,9,17,21/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,0,1,15,g,9,17/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,0,1,15,g,15,17/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,0,1,15,g,17,22/` | gate（accepted）, gate_nolearn（accepted）, sub（accepted） |
| `13,0,1,15,g,9,22/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,0,1,15,g,17,21,22/` | gate_nolearn（accepted） |
| `13,0,1,15,g,9,21,22/` | gate_nolearn（accepted） |
| `13,0,1,15,g,15,21,22/` | gate_nolearn（accepted） |
| `13,0,1,15,g,9,24/` | gate_nolearn（accepted） |
| `13,0,1,13,g,15,22/` | gate_nofilter（accepted） |
| `13,0,1,15,g,15,17,18/` | gate_nofilter（accepted） |
| `13,0,1,15,g,18,22/` | gate_nofilter（accepted） |
| `13,0,1,15,g,2,4,22/` | gate_nofilter（accepted） |
| `13,0,1,15,g,4,15,17/` | gate_nofilter（accepted） |
| `13,0,1,15,g,4,17,22/` | gate_nofilter（accepted） |
| `13,0,1,15,g,4,15,22/` | gate_nofilter（accepted） |
| `13,0,1,15,g,4,22/` | gate_nofilter（accepted） |
| `13,0,1,15,g,15,22/` | gate_nofilter（accepted） |
| `13,0,1,13,g,9/6+11` | sub（accepted） |
| `13,0,1,15,g,9,21/6+11` | sub（accepted） |
| `13,0,1,15,g,15,21/6+11` | sub（accepted） |
| `13,0,1,15,g,9/6+11` | sub（accepted） |
| `13,1,0,14,g,18,25/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,1,0,14,g,2,4,25/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,1,0,14,g,18/8+16` | sub（accepted） |
| `13,1,1,13,w,18` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,1,1,15,g,19,21/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `13,1,1,15,g,21/3+5` | sub（accepted） |
| `13,1,1,15,g,19/7+15` | sub（accepted） |
| `14,0,0,15,g,21,27/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `14,0,0,15,g,21/19+24` | sub（accepted） |
| `14,0,0,15,g,27/7+15` | sub（accepted） |
| `14,0,1,14,w,27` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `14,0,1,14,g,18,25/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `14,0,1,14,g,2,4,25/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `14,0,1,14,g,18/8+16` | sub（accepted） |
| `14,1,0,15,g,21,27/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `14,1,0,15,g,27/7+15` | sub（accepted） |
| `14,1,0,15,g,21/19+24` | sub（accepted） |
| `14,1,1,14,w,21` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `14,1,1,14,g,6,14/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `14,1,1,14,g,6,23/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `14,1,1,14,g,4,6,8/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `14,1,1,14,g,4,6,25/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `14,1,1,14,g,6,17,23/` | gate_nofilter（accepted） |
| `15,0,1,15,w,29` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `15,0,1,15,g,21,27/` | gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
| `15,0,1,15,g,27/7+15` | sub（accepted） |
| `15,0,1,15,g,21/19+24` | sub（accepted） |
| `15,1,1,15,w,12` | l0（accepted）, l1（accepted）, l2（accepted）, wire（accepted）, gate（accepted）, gate_nolearn（accepted）, gate_nofilter（accepted）, sub（accepted） |
