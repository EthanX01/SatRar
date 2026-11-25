# SatRAR Circuit V2 進度報告大綱

**進度截點**　2026-11-25  
**建議規模**　主簡報 10 頁，約 10 分鐘。可依報告時間增減附錄。  
**報告主軸**　V2 的 gate-native SAT、替代邏輯搜尋與獨立等價驗證已串成可重跑的流程。現階段有小型 fixtures 和 Figure 1 的驗證結果，接下來要量測大型電路與 V1／V2 差異。

## 01 封面

- 標題：SatRAR Circuit V2 實作進度
- 副標：AIG 原生 SAT 求解與等價改寫
- 報告人、場合與日期可補入。

## 02 專案目標與目前進度

- 任務是為指定的 AND 輸入線尋找替代線路，再移除原線，同時保持全部輸出的行為。
- 已完成 AAG 讀寫、自製求解器、故障分析、候選搜尋、兩階段 CEC 及改寫輸出。
- 目前的驗收證據是 Release／Debug 各 6 項測試及 Figure 1 的跨模式驗證。

## 03 程式架構與資料流

- AIG 層用 `Circuit`、`Literal`、`WireRef` 保存電路、極性與接收腳。
- Solver 層用 `GateNet` 編譯鄰接關係，由 `SolverSession` 維護查詢狀態。
- Analysis／RAR 層用 `FaultContext` 算必要指派，由 `SatRarEngine` 產生候選，交給 `Rewriter` 改線。
- Verify 層用 `RarVerifier`、`EquivalenceChecker` 與 CaDiCaL 驗證，`exportVerifiedAag` 發布檔案。
- 圖表重點：從 `read` 到輸出 AAG，標出原生 AIG 搜尋與 CNF CEC 的分工。

## 04 自製 SAT 引擎

- `GateNet` 用緊密的訊號索引與 gate 鄰接表，讓指派只喚起相關的 gate。
- `SolverSession` 保存 trail、決策層與理由。用 `z = x ∧ y` 示範 `z=0、x=1` 推出 `y=0`，並記下理由 `z ∨ ¬x ∨ ¬y`。
- RAR 試探用前提、傳播和回復。完整 SAT 查詢支援 DPLL 或 1-UIP CDCL，達搜尋上限可回報 UNKNOWN。
- 可依聽眾深度另加一頁 trail 與 backjump 的逐步例子。

## 05 故障偵測與候選搜尋

- 對目標 pin stuck-at fault 推導必要指派（MA），再比較下游 postdominator 的 output sa0 所需 MA。
- `l0/l1/l2` 使用直接或遞迴推論。`wire/gate/sub` 透過試探衝突或反向推論組成替代訊號或子電路。
- 有重匯合時，原生 fault miter 可顯示 good 與 faulty 電路是否有輸出差異。Exact 查詢用這個差異條件求見證輸入。

## 06 Figure 1 改寫示例

- Target 為 `g6→g7` 接收腳 pin 0 sa1，示例候選為 `g5→g9`。
- `l2` 與 `wire` 都找得到此候選，`l0` 與 `l1` 在這個 fixture 中找不到。
- `Rewriter` 先加入替代邏輯得到 N1，再把目標腳固定為 stuck-at 值並簡化得到 N2。
- 圖表重點：標出原目標線、替代線與接入位置。

## 07 兩階段等價驗證與輸出

- 第一階段比較原電路 N0 與加線後 N1，第二階段比較 N1 與移除目標後 N2。
- CNF miter 問是否存在輸出相異的輸入。CaDiCaL 回傳 UNSAT 表示等價，SAT 時重播反例。
- `rar_apply` 把通過的改寫寫到暫存 AAG，重讀並與 N0 再驗一次，成功後發布。

## 08 測試結果與覆蓋

- Release **6/6**、Debug **6/6** 通過，兩組都執行 CaDiCaL 測試。
- 測試涵蓋 gate BCP 真值表、fault MA、定理錨點、exact SAT／witness、AAG round-trip 與 CEC。
- Figure 1 的 target sa1、g7 sa0、g9 sa0 分別有 **3、3、7** 組偵測向量，由六個 PI 的窮舉得到。

## 09 Figure 1 模式比較

- 八種模式在同一 target 合計 **92 筆輸出全部通過 CEC**，含跨模式重複候選。
- L2 與 wire 各找到相同的 **4 個 canonical keys**，分別用了 **28** 和 **9** 次試探。
- Gate 啟用 filter 時為 **13 次試探、16 筆候選**，關閉 filter 時為 **51 次試探、24 筆候選**。這是單一 fixture 的探索範圍與工作量比較。
- 圖表可放四個主要數字，八模式完整表移至附錄。

## 10 下一階段驗證

- 跑 C432–b19，記錄完成、達限與等價驗證結果，按規模比較搜尋及 CEC 成本。
- 用相同輸入和設定執行 V1、V2，對照 canonical keys、候選覆蓋、trial 與實際耗時。
- 現階段已能展示小型電路的完整改寫流程。大型基準與 V1／V2 效能仍待量測。

## 可選附錄

- **A．八模式完整數據**：候選數、trial、learned clauses、gate visits、CEC accepted。
- **B．程式閱讀索引**：`GateNet`、`SolverSession`、`FaultContext`、`SatRarEngine`、`Rewriter` 與 `RarVerifier` 的公開函式和責任。
- **C．CLI Demo**：依序展示 `read`、`fault_ma`、`rar_verify`、`rar_apply`。

## 資料來源

- [V2 程式導讀](V2_CODE_WALKTHROUGH.md)
- [V2 實作狀態](V2_STATUS.md)
- [Figure 1 模式統計](../results/v5/fig1_modes.tsv)
