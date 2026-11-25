# SatRAR 後續實作規格供 Claude／Cursor 使用

2026-11-25 · EdaLab／`satrar-cpp` · C++17、CMake、Windows x64

## 0. 接手任務

在現有 `satrar-cpp/` 繼續實作。先修可定位的問題，再用原論文的 fixture 和定理前提驗收搜尋，最後完成可重跑的實驗。依序處理 R1–R7。R8 只有在演算法驗收或有來源可支持的實驗需求出現時才啟動。每個階段須交付修改路徑、重跑命令、輸出位置及尚未通過的條目。可從工作區根目錄直接引用本規格作為任務提示。

| 項目 | 約定 |
|---|---|
| 專案目錄 | `C:/Users/User/Documents/file/NTU/EdaLab/satrar-cpp`。本文件中的程式路徑相對於該目錄，命令另標示執行位置。 |
| 工程入口 | `CMakeLists.txt`、`README.md`、`src/app/CommandShell.cpp`、`scripts/rar_batch.py`、`results/PHASE_STATUS.md`。 |
| 主證據 | 工作區根目錄的 `ASPDAC-2009-SatRAR.pdf`，Wu、Lin、Huang、Huang，ASPDAC 2009，印刷頁 p.191–196。以論文定義、圖、演算法和經驗性實驗設定為準。 |
| 輔助證據 | `11. Applications of SAT on Logic Syn.pdf`，PDF 第 22–30 頁為 2-Way RAR，第 31–40 頁為 SatRAR。`07.  Advanced SAT Techniques.pdf` 的 recursive learning 說明用於核對基準。講義範例的候選可和論文不同。 |
| 歷史規劃 | `docs/Cursor_Composer_SatRAR_Plan_v2.md`、`docs/SatRAR_Core_Consistency_Audit_2026-11-24.md` 記錄早期狀態。以目前程式和 `results/PHASE_STATUS.md` 判定待辦。 |
| 工程邊界 | 保留 ASCII AAG、pin 級故障、既有 CLI 與現有 vendored CaDiCaL。`minisat-solver/` 只讀。專案外實作、全量格式化和與任務無關的重構均不在此規格。 |

### 目前已通過的基線

- 既有記錄中，Release 與 Debug 各 17/17 CTest 通過。CaDiCaL 2.1.0 可在原生 Windows x64 執行。Figure 1 的八種模式共 623 筆候選，各通過加入與移除 CEC。指定 `g5 → g9` 可輸出 AAG，再載入後等價。
- 已對 60 張小型 AIG 的 169 個候選做獨立 PI 真值窮舉。EPFL `ctrl`、`dec` 有 smoke 結果。ASPDAC 的 12 個輸入已取得 11 個，四 pin smoke 有 76/77 個輸入與模式組合完成。C432、C499、C6288、b01、b13 已跑完七種模式的 discovery。大型案例仍有部分模式限時或限額，b19 檔案待補。
- AIG 上的算法行為、原論文 Table I／II 數字重現和 Figure 7 效能重現各有自己的驗收。以本文件的證據矩陣判定完成範圍。623 是現有回歸數，修正不完整的學習後允許合法候選集合改變，變更須附逐候選理由和 CEC。

### 排程與完成條件

| 優先 | 任務 | 主要程式 | 合併條件 |
|---|---|---|---|
| P0 | R1 修正兩階段 CEC 的錯誤歸屬與驗證狀態 | `src/rar/RarVerifier.cpp`、`src/app/CommandShell.cpp` | 注入第二階段失敗仍能保留第一階段證據。所有不可判定情況禁止發布。 |
| P0 | R2 驗證 fault、MA、postdominator 及 Figure 6 可逆前綴 | `src/analysis/`、`tests/test_fault.cpp`、`tests/test_circuit_propagator.cpp` | 小電路獨立 oracle 驗證 soundness，Figure 1／6 錨點齊全。 |
| P0 | R3 修正 conflict learning 的生效時點與定理 provenance | `src/rar/SatRarEngine.cpp`、`src/analysis/CircuitPropagator.cpp` | 同一 dominator 的後續 trial 可使用安全 learned rule。所有模式的定理標籤可重播。 |
| P1 | R4 補強 Theorem 3 並讓資源限額涵蓋雙向搜尋 | `src/rar/SatRarEngine.cpp`、`tests/test_rar_discover.cpp` | 有獨立多輸入 AND–OR 正例、拒絕反例、清楚的搜尋截斷狀態。 |
| P1 | R5 修正實驗狀態、覆蓋率與續跑聚合 | `scripts/rar_batch.py`、`src/app/CommandShell.cpp` | 可驗證完整覆蓋，錯誤與限額可同時保留，分片合併和重跑一致。 |
| P1 | R6 補齊輸入來源、轉換和 wire 計數對照 | `scripts/bench_to_aag.py`、`benchmarks/manifests/` | b19 已驗證或有明確輸入阻塞。每個實驗點能追溯到來源及 AIG 拓樸。 |
| P2 | R7 完成同引擎的 Table I／II、Figure 7 對照與效能量測 | `scripts/rar_batch.py`、`results/`、`docs/experiment-protocol.md` | 能依來源、scope、mode、覆蓋率和環境解讀每一格。 |
| 條件式 | R8 校正可比拓樸，或增加帶證明的候選覆蓋 | `src/analysis/GraphAnalysis.cpp`、轉換／證明介面 | 有可驗證的拓樸或 proof 證據，另有等價回歸和獨立結果欄。 |

## 1. 共同語意及論文追蹤表

| 原文錨點 | 實作契約 | 最小可重跑證據 |
|---|---|---|
| §II.A、Fig. 1，p.191 | `PinFault(w,s)` 指定接收 gate 的單一輸入 pin。良好電路中帶符號的 pin 值為 `1-s` 才能激發。完整偵測條件為至少一個 PO 不同。MA 是該偵測條件蘊含的良好電路指派。 | 每組 PI 分別模擬 good／faulty，包含同 source 的其他 pin、反相 edge、多 PO 及常數。 |
| §II.B–C、Fig. 2、Example 4，pp.191–192 | Add 建立新冗餘，再 Remove 使原 target 消失。2-Way RAR 對 target 和 dominator 控制型 output fault 比較反向 MA。L0 用直接推論，L1／L2 是遞迴學習深度。 | `g5 → g9` 是 L2 的 fixture 命中，L0／L1 無此候選。兩個等價關係各自檢驗。 |
| §III.A、Theorem 1、Example 5，p.193 | 從 `Φt − Φd` 選單一 signed 指派，在 `Φd` 導致衝突時導出 wire。兩側已有相反 MA 則屬既有 2-Way 直接案例。 | `g5=0` 的衝突及 root 證據可重播，且輸出同一個 `g5 → g9`。 |
| §III.B、Theorem 2、Cor. 2.1／2.2，pp.193–194 | 使用反向推論，或由真的參與衝突的 MA trial 形成 AND。以實際支持集分類，保留 signed operands。 | Fig. 1 的 OR 候選及獨立雙 root conflict、兩個 decisions 加一個反向推論正例。 |
| §III.C、Theorem 3，p.194 | `S = AND(A, OR(B))`，target 上 `S=1`，dominator context 上 `S=0`。每個 OR operand 的反面均由 dominator 的試探前提導出。 | 雙向重播、刪失最後一個 dominator 蘊含時的拒絕、兩段 CEC。 |
| §IV、Fig. 5／6，p.195 | 使用 circuit structure 的 BCP、決策層、反向理由、衝突學習和 implication filter。各故障 context 共用可保留的傳播前綴，trial rollback 可驗證。 | `f=1`、`g4=0` 的前綴帶原有 reason。試探後退回，保留同一基準。學習和 filter 的 ablation 可分開觀察。 |
| §V、Table I／II、Fig. 7，pp.195–196 | 同一引擎比較模式，在 benchmark 排除直接接收 gate 作 attachment，掃描原定義範圍，記錄 #wire／#tar／#alt／#T。 | 12 個 case、7 種模式、資料來源和計數口徑。每個已報告完成點有完整搜尋覆蓋。 |

### 統一符號與證明方向

- `F_t`、`F_d` 分別為 target pin fault、dominator 控制型 output fault 的完整 good／faulty 偵測關係。`Φ_t`、`Φ_d` 是 BCP 或指定深度學習導出的必要條件集合。`F ⇒ Φ`，因此 `Φ ∧ ¬l` refuted 可支持 `F ⇒ l`。未找到衝突的 bounded propagation 狀態記為未定。精確 SAT witness 必須在獨立模擬中找到改變的 PO。
- AIG raw literal `2v` 是正相，`2v+1` 是反相，raw `0/1` 是常數。AND raw node 的控制型 output fault 為 sa0。若論文的邏輯 gate 是 complemented AND 表示的 OR，邏輯 output sa1 映到 raw AND sa0。發佈證據須標出 raw 與論文 gate 名稱對應。
- 對候選 separator `S`，證明 `Φ_t ⇒ S` 與 `Φ_d ⇒ ¬S`。接入 raw AND 的 enabling condition 是 `¬S`，使 `g_d' = g_d ∧ ¬S`。Wire 的發射值因此是 target MA literal 的反相。Gate／Sub 交給現有 `Rewriter` 將 separator 轉成 enabling condition。反相 fanout、直接 PO 和其他 fanout 皆由帶符號 substitution 保持原相位。
- Theorem 2 p.193 的式 (2) 排版將結論主詞印成 `MA(w_t)`，按前提與 De Morgan 推導應為 `MA(g_d) ⇒ ¬g_n`。Theorem 3 p.194 的證明式 (3) 展開到 `b_(n-1)`，定理前提包含 `b_n` 的反面。實作必須驗證 **所有** `¬b_i`，否則不能推出 dominator 上 `OR(B)=0`。兩處差異寫入 `docs/source-map.md`，測試證明方向。

## 2. R1｜嚴格保留兩段改線與 CEC 證據

**對齊** §I、§II 的 RAR，使用 exact CEC 作為本 AIG 實作的提交依據。`N0` 為原始電路，`N1` 為建立 alternative 後、原 target pin 仍在的電路，`N2` 為將該 pin 接到指定 stuck 值後再化簡的電路。只有 `N0≡N1` 和 `N1≡N2` 均成立時接受改線。提交檔案還需驗證暫存 AAG 重新載入後等價。

1. 保留 `Rewriter::addAlternative` 的 postdominator 和 cycle 檢查。拆開改線構造與 CEC 階段的錯誤處理。現況 `RarVerifier::verify` 的外層 catch 一律覆寫 `addition.verdict`。若 `removeTarget` 或第二次 `check` 拋例外，會抹去已完成的 `N0≡N1`。改為第二階段自己的 `InvalidInput` 或 `BackendError` 記錄，並保留 `addition` 和 `failed_stage`。沿用既有 result 型別，欄位不足才擴充。
2. 每階段只有可信的 UNSAT 得出等價。SAT 依 AAG PI 位置映射並用結構模擬重播 witness，回報不同的 PO 位置。SAT 輸出損壞、退出碼不一致、witness 無法重播、逾時、後端不存在均需攜帶原因。狀態 `Accepted`、`Rejected`、`Indeterminate` 分別表示兩次等價、模型驗證出功能改變、其餘不可判定。若現有枚舉名稱不同，保留 API 並在 CLI 以穩定狀態碼映射。
3. `rar_apply` 只對目前電路快照的候選執行。現況會重讀記憶體中的序列化內容並做 round-trip CEC，之後直接 `ofstream` 寫最終路徑，I/O 失敗可能留下截斷檔。改為先寫同目錄的暫存 AAG，關閉檔案、從磁碟重讀並做 round-trip CEC，再安全發布。模擬寫入／發布錯誤並確認舊目的檔保留。回傳的目標後處理結果必須是組合 DAG，PI 順序、PO 順序和反相語意以原介面為準。
4. 在 `test_rewriter`／`test_equivalence` 加上可控制的後端或既有 mock，覆蓋 `N0≡N1` 後第二次丟錯、合法 SAT witness 在第二階段拒絕、`N0≡N2` 但 `N1` 不等價的反例。保留常數 0 vs. 1、PI renumber、PI 對調、PI/internal ID 碰撞、第二 PO、同一 driver 的不同 pin、PO fanout 帶反相、多次並行暫存隔離與 cyclic attach 測試。

```cpp
// 架構草碼。沿用現有 EquivalenceChecker、Rewriter 與 verdict 型別。
result.addition = verifyStage(original, [&] { return addAlternative(original, alt); });
if (!result.addition.isEquivalent()) return finish(result, Stage::Addition);
result.removal = verifyStage(result.addition.netlist, [&] {
    return removeTarget(result.addition.netlist, targetOf(alt));
});
if (!result.removal->isEquivalent()) return finish(result, Stage::Removal);
return acceptedWithRewrite(result);
```

## 3. R2｜證明 fault context、MA 與共享前綴

**對齊** §II.A、§IV Fig. 5／6 及講義 11 的 Fault Propagation／Mandatory Assignment。保留目前的 native AND 推論、postdominator 快取、故障錐投影及獨立 full miter。下列項目以差分測試為先，有反例才縮小修補對應層。

1. `GraphAnalysis::outputPostdominators(start)` 對所有可達 PO 用虛擬共同 sink，檢查每條 path。論文 `gd0` 為較遠的輸出端，`gdk` 為接收 gate。現有 API 以接收端到輸出端排序，建立 propagation prefix 時逆序走。虛擬 sink 不產生 candidate。死線標示不可觀測，空 dominator 集須與無合法輸出比較確認。多 PO 跨分支時不能把單條 PO 路徑的 gate 當共同 dominator。
2. Fault pin 的 excitation 用 signed driver。故障版只覆蓋對應 pin，其他分支繼續看原 source。output fault 在故障版覆蓋 raw node fanout 及直接 PO，保持負相消費者語意。單一受影響 fanin 的 raw AND，未受影響側必為 `1` 才可傳播。兩個 fanin 都可能受到故障時走目前的 projection，不能任選一邊固定為 `1`。
3. `FaultContext::projectFaultImplications` 的 BCP 可導出必要條件或證明衝突。驗證優化後的投影以 affected PO 的 backward cone 為界，且未受影響的節點在雙副本共享。雙副本 exact miter 是獨立 oracle。`FaultStatus::Consistent` 表示目前推論無衝突，`NeedsExactTest` 表示尚待 exact 判斷。`extra` 為試探時，所得 trail 是條件性結論，必須顯示與 base MAs 的差異。base 已衝突可記已冗餘，避免 vacuous MAs 遍歷產出候選。
4. 擴充 `test_fault` 或小型差分測試，依固定 seed 生成可窮舉的組合 AIG。對每個選中的 `(destination,pin,sa)` 以及 output fault，結構模擬枚舉所有 PI，核對每個被報為 MA 的 literal 都對全部偵測輸入成立。用 exact miter 驗證 `D_fault ∧ ¬ma` 為 UNSAT。反過來容許存在 BCP 漏掉的 MA，因為推論策略是 bounded。至少包含路徑分裂再匯合、抵消的重匯合、兩個 PO 僅一個變化、signed duplicate edge、常數 pin、不可觀測 gate 及 inactive fault。
5. `CircuitPropagator` 的 trail reason 能回溯到確切 gate、root unit 或 scope 前提。對 `target → g7 → g8 → g9` 的上下文切換，比較 fresh propagator 的 closure、共享部分的 trail 長度與 reason。增加同 prefix 的不同 suffix、短 prefix、empty prefix、上一次為 conflict、巢狀 trial 和跨 fault 的測試。用 RAII checkpoint 或現有 scoped API 保證提早返回也回復 pending cursor、conflict marker 和已刪除的 reason。Fig. 6 的 level 0 是示意的故障傳播層，證據輸出保留與實際 solver root 的映射。
6. Figure 5 line 4–5 和 9–10 對 target 或 dominator 的冗餘有明確事件。現行 target 只有 `targetConflict`，dominator 衝突直接跳過。加入可觀測的 `redundant_target`／`redundant_dominator`（附種類與位置）。教學 trace 可按 Fig. 5 在支配點衝突時結束該次示範。批次若繼續搜尋其他支配點，須記錄該策略及事件，且不可將此診斷當作一筆 alternative 或已執行的重寫。

### Fig. 1／6 的固定語意錨點

| 觀察點 | 預期 |
|---|---|
| Fixture | `tests/fixtures/paper/fig1.aag`，`a,b,c,d,e,f` 為 PI var 1–6，兩個 PO 依序為 g5、g9。g1–g9 的邏輯 raw 依序為 `14,16,18,20,23,25,26,29,30`。 |
| Target | `g6→g7` 是接收 var 13 的 pin 0，sa1。`g6=0,g2=0,d=0,g1=0,g4=0,g5=0,g3=1,f=1,a=1,b=1` 均為 MA。原電路推到的其他必要值可以出現在完整 snapshot。 |
| Fig. 6 共用前綴 | 先 `f=1`，再 `g4=0`。切換到 g7 output sa0，保留前兩項及理由。繼續得到 `g7=1,g6=1,g3=1,a=1,b=1`。 |
| g7 下試探 `d=0` | 無 BCP 衝突，推出 `g2=1,g5=1,e=1,g1=0,c=0`。回溯後移除只由該 trial 造成的指派，filter 可把 `g1=0` 從同一 context 的單步候選試探清單移除。 |
| g9 下試探 `g5=0` | g9 raw output sa0 需要 `g9=1,g8=1,f=1`。加入 `g5=0` 有衝突，wire attachment var 15，source raw 23。L2 可從 recursive splitting 推得相反 MA，L0／L1 缺同一候選。 |
| Gate 範例 | 論文 Example 6 的非平凡候選為 `g5∨d` 接 g7，separator raw `{22,9}`。`g2∨d` 接 g7 為同例的平凡候選。講義另有 `g1∨g2` 接 g9，separator raw `{15,17}`，分開檢查。 |
| 真值 oracle | 64 個 PI vectors，檢查兩個 PO。target sa1、g7 sa0、g9 sa0 的可偵測向量數依序為 `3,3,7`。g9 sa0 加 `g5=0` 為 `0`。紙本與講義指定的重寫各做兩段獨立窮舉和 CEC。 |

## 4. R3｜補齊 Figure 5 的即時學習、理由和模式隔離

**對齊** §II.C、§III.A–B、§IV Fig. 5 line 12–24。現在已具備三類 discovery，主要缺口是 `learnAll` 在一個 dominator 探索結束才執行，且會 `reset()` 整條 trail。同一 context 的後續 decision 用不到該批新規則，與 Fig. 5 的 conflict learning 時點有差距。

1. 在 `CircuitPropagator` 支援於安全 checkpoint 插入一條已證明的 rule。先從 conflict graph 保存穩定的理由快照，再退出 trial 到可用的 dominator base 或可證明的 parent。安裝 rule 後，主動在現有 trail 上檢查這條 rule 的滿足、unit 或 conflict，將新 unit 加到正確 level 後 propagate。回復到較短 prefix 時，仍要重播可能被回溯移除的 learned unit。可在第一版退回 dominator base 然後重放尚未完成的 trial path。若要實作 backjump，先用差分測試顯示與基準等價。
2. 學到的條款若使用 `Circuit ∧ contextRoots ∧ decisions ⇒ ⊥`，則存 `¬contextRoots ∨ ¬decisions`。絕對不能把僅對 fault context 成立的 `¬decisions` 加成全域規則。可在同一 dominator 用明確作用域和撤銷，或沿用含所有前提的 circuit-valid 條款。投影 BCP 的投射值若以假設加入，必須出現在 guard 或有足以重播的推論理由。跨 fault、跨 AIG snapshot、跨 ablation 的初始 learned 狀態要明確，預設各模式 cold run，維持分片／非分片一致性。
3. Theorem provenance 以實際 proof support 編碼。現況用 `trial.size()` 標記，可能把只依賴一個 root 的衝突寫成 `Corollary-2.1`，或把一個 root 導出反向 MA 的案例誤標 `2.2`。用穩定去重後的支援 root 數和事件類型判定。`Conflict` 一個 root 為 T1、兩個 root 為 Cor. 2.1。`ReverseImplication` 一個 root 加一個反向 MA operand 為 T2，兩個以上支持 root 加該 operand 為 Cor. 2.2。多 root conflict 超過論文 Cor. 2.1 的兩個 root 時標為經同一蘊含證明導出的 generalized conjunction。零 trial root 代表 base 已矛盾或既有基準推論，走 base 診斷或 direct opposite MA，勿造假 provenance。
4. 將別名和正文對應固定。`l0` 只用直接 MA，`l1/l2` 分別以 1／2 層 unresolved justification split，對 `z=0, z=x∧y` 的 `x=0`／`y=0` 分支取仍可行分支的共同推論，受限中止時不可當作兩枝皆已證明。`wire` 只產生 direct opposite 與 single-root conflict wire。`gate` 加 reverse implication 和 conflict-supported AND。`gate_nolearn` 只停用 learned rule 安裝，保留當下衝突／逆推的 candidate 發射。`gate_nofilter` 只停用 candidate trial 清單的 implication filter。`sub` 才跑 R4 雙向搜尋。
5. Filter 僅基於該 dominator context 的成功單步 probe 新導出的同相 target MA literals。已有 base assignments、剛試探的 literal 本身、未確定有 reason 的值不要納入。長探查、回溯、換支配點後不得沿用錯誤的 filter 狀態。若 learning 更新 base，須保有同一套 guard／scope 保證或重算目前 filter。解除 filter 可擴大涵蓋。有限 budget 或不同搜尋順序下以實際證據驗收，勿將 candidate 數單調增加寫成數學不變量。
6. Candidate 正規化與 proof provenance 分離。去重 key 使用 pin、stuck value、attachment、signed expression。替換模式與 variant 各自計數，兩種推導可更新同一候選的 evidence。強制可選 trace 輸出有穩定格式的 target/dom、scope、ordered context roots、trial roots、reason/witness、kind、signed operands、canonical key、remaining budget、filter 與 learned unit 事件。一般批次採 summary 時不替每一筆寫 trace。

```cpp
// 示意整合流程，沿用 FaultContext／CircuitPropagator 的現有所有權。
const auto base = prop.contextLevel();
TrialProof proof;
{
    ScopedTrial trial(prop, decisions);
    proof = captureProof(prop, trial, targetMa, domMa);  // 在回溯前複製 reasons
}
emitFromProof(proof, targetMa, domMa, eligible);        // 類型依 support 判定
if (options.enableLearning && proof.isConflict()) {
    prop.cancelToContextLevel(base);
    applyGuardedLearnedRule(prop, proof.contextRoots, proof.trialRoots);
    checkUpdatedBaseAndContinue();
}
```

**驗收** 使用一個需要兩個 trial roots 才 BCP 衝突的 fixture，下一個單步 probe 可因剛學到的 guarded rule 產生 unit 或更早衝突。切換為另一個支配點或 active fault 後該 rule 仍符合證據所列 guard。用獨立 exact solver 或窮舉證明 learned clause 為 circuit entailment；scoped clause 則加上 scope 前提再檢查。重跑 `gate_nolearn`，當下發射仍能發生，learn 計數為零。為 Cor. 2.1 和 2.2 各保留一個不靠 Figure 1 的 reason fixture。所有已導出候選再跑 R1，數量變化用 key diff 說明。

## 5. R4｜Theorem 3 雙向證明、搜尋預算與可驗收反例

**對齊** §III.C p.194。現況 `trySubCircuit` 選同一 dominator 的 trial trail 上的 literals，對 target context 反向試探，並收縮 OR operands。需要保存兩側證據。現在 OR proof 和縮減 retry 沒有計入 `maxTrials`，在大型 AIG 上會讓 `sub` 實際試探量超出回報的 per-fault limit。

1. `A` 由 target MA 的 signed literals 組成。先在 `Φ_d ∧ A` 傳播且無衝突，擷取該 context 下選定的 `¬b_i` 並保存 provenance。`B` 需能證明 `Φ_t ∧ ∧¬b_i` 衝突。重播可用 `¬b_1…¬b_(n−1)` 推出 `b_n`，也可保留「全體反面導致衝突」的等價證明，標明所用式。
2. 從 `A` 去除 reason 中沒有用到的 literal，或縮小 `B` 後，重跑兩側檢驗，維持 deterministic canonical order。Reject 或 skip 空 `A`、無證明的 `B`、未知／cycle 來源及語意矛盾的無效 operands。只剩一個 `B` 時按有效 proof 退化到 T2／Cor. 2.2 的 conjunction 或去重。保留 `n≥2` 的 genuine AND–OR fixture。
3. 共用 budget 給外層試探、遞迴 split、`sub` 的每次 reverse operation 以及 operand deletion retry。明確定義每種事件計數。一次完整 recheck 可由多個 decision 構成，按使用的 operation plan 定義並記錄。耗盡時停止入新 branch，rollback 乾淨。只保留已完成證明的候選，回報 `search_limited`、截至哪個 scope、何種子步驟達到上限。獨立 CEC 的 timeout 是另一個欄位。
4. 結果記錄最大 decision 深度、嘗試數、雙向 proof probe 數、削減次數和截止原因。Tier 1 到此即可涵蓋論文實際提出的兩級式。更深 alternating construction 只有在有獨立可重播的雙側證明和 fixture 時加入擴充模式，單獨統計。

```cpp
// A、B 均為 signed literals，refute() 區分 Refuted／Open／Limited。
const auto underDom = propagateUnder(domBase, A, budget);
if (!underDom.open()) return noSubCandidate(underDom);
auto B = selectComplements(underDom.impliedWithReasons(), targetBase);
const auto back = refute(targetBase, negateAll(B), budget);
if (back.refuted() && allComplementsProved(underDom, B)) {
    emitSubCircuit(A, B, captureBothSides(underDom, back));
}
```

**驗收** 建立一個最少兩個 `B` 仍成立的 fixture，附真值表或 exact implication queries。再移掉 `¬b_n` 的 dominator 證據，給出 `Φ_d∧A∧b_n` 可滿足的反例並確認拒絕。加入因 budget 在 B shrink 中止而不能宣告完整搜尋的測試。每個真實 AND–OR 發射至少在 fixture 兩階段 CEC 和獨立窮舉通過。

## 6. R5｜修正批次完整性與可續跑記帳

**對齊** §V 的逐 wire 實驗與表格範圍。`rar_batch.py` 已有 `--shard-wires`、`--summary-only` 和依 binary/hash/setting 的 checkpoint。保留既有介面，修正下列可重現問題。

| 現況 | 修改契約 | 驗收 |
|---|---|---|
| `execute()` 的 `verification_incomplete` 可覆寫 `search_limited`。 | 保存兩個獨立欄，另保留相容的 `status` 作摘要。區分 `complete`、`partial`、`timeout`、`input_error`、`process_error` 以及 CEC 的 `unchecked`／`incomplete`。 | 偽造限額且有 unknown CEC 的 shard，兩個原因都出現在結果。 |
| `table_I.tsv`／`table_II.tsv` 用其中一個 mode 的 `searched_wires` 代表整列。 | 保留共同 `#wire`，每個 mode 都列 `searched_wires`、`complete_wires`、狀態與計數。`--wire-limit` 四 pin smoke 不得畫成 full run。限制正好落在一個 pin 的某個 sa case 時，該 pin 屬已進入且未完整。 | 同一 circuit 兩個模式不同 coverage、其中一個 timeout，表格與 raw record 一致。 |
| `run_cached()` 遇到缺 summary 的 shard 就 `break`，且後到的失敗可能蓋過早先原因。 | 保存所有 shard 的起訖和 outcome。對能獨立跑的其餘區間繼續，或明確記錄尚未排程。恢復時只 rerun 未完整區間，可合併已完成 checkpoint。 | 4 個分片中第二片 timeout，後兩片可保存。恢復第二片後，非時間欄與單次完整掃描逐筆相同。 |
| 只檢查一行 summary 的欄數，既有 cache JSON 缺 schema 檢查。 | 依 schema version 驗證計數非負、合法 status、mode、binary/input 指紋、起訖區間和可行的 coverage。損毀 cache 單獨保存並重算，避免讀取假 `ok`。保留既有 JSON 原子替換，最終 TSV 也以暫存後替換。 | 截斷或偽造 JSON、重疊 range、候選列重複、非整數欄皆能定位，且沒將損失資料當成完整。 |
| 大型非 summary 執行把 CLI 全量 stdout、解析列及所有 TSV 資料留在記憶體。 | 以可量測的記憶體門檻決定串流輸出及逐分片寫入。逐候選產物採穩定 key，每個 shard 先完整寫檔才 commit metadata。Summary 路徑僅聚合確定的計數。 | 大型 smoke 記錄 peak RSS，分片結果和小型 monolithic 的 canonical rows 相同。 |

**完整性判定**。一個 `(input_digest, topology_profile, scope, mode, settings)` 的 full discovery 需區間無重疊、沒有缺口、涵蓋所有 eligible pin，且每 pin 的兩個 stuck 值都完成搜尋。驗證階段允許獨立分析，`--discover-only` 的表格只代表 discovery。報告數字保留 `discovered_candidates, accepted, rejected, unverified`；部分資料另欄記實際覆蓋與中止原因。去重跨分片時用穩定的 canonical key，分片 partition 以 pin 為單位，避免把同一 pin 的 stuck 值切開。

**Checkpoint 指紋**。維持 CLI、solver 和兩份輸入的 SHA-256，加入 schema/converter version、source profile、是否 cut、decision depth、learning、filter、scope、pin order ID、budget 和 benchmark profile。若續跑只調高 timeout／trial cap，僅在舊 shard 已能證明相同搜尋語意和完整覆蓋時復用，且另記 provenance。報告同時保留原始時間與 cache reuse，跑 Fig. 7 計時時全用 fresh run。

## 7. R6｜補輸入並核對來源拓樸與論文口徑

**對齊** §V pp.195–196。Table I／II 的 circuits 為 `C432 C499 C3540 C6288 C7552 b01 b13 b14 b15 b17 b18 b19`。輸入存在性、來源校驗、AIG 轉換正確性和與論文原始拓樸可比性各需證據。

1. 補 `benchmarks/inputs/itc99/b19.bench`。用 `benchmarks/manifests/aspdac2009.json` 的 `source_url`、blob `150930c1502857fe981527df3c5da6aff2e72a3a`，已知長度 `14,569,044` bytes。核對 `SHA1(b"blob " + ascii_size + b"\0" + raw_bytes)`，再記 SHA-256。可用可連線環境的 `--fetch-missing` 或手動提供同 blob 的原始 BENCH。下載時分段或換 mirror 均須驗證合併後的完整 bytes。先驗證再寫入，保留既有合法輸入。原工作區的下載受檔案大小及 DNS 阻塞，輸入仍不可得時保留 `input_error` 及細分 `missing_input` 證據，繼續其餘 case。來源 [b19 BENCH](https://github.com/AnmolSinghNarwariya/ITC99_benchmark_circuits/blob/main/b19/b19.bench)。
2. BENCH 到 AAG 的每個 case 輸出 manifest、git blob、來源及轉換 hash、原始 gate fanin arity、原始接線總數、原始 PI／PO 順序、DFF cut 次序、AIG 變數映射與 transformed pin 數。維持 Q 作額外 PI、D 作額外 PO 的明確 combinational cut。驗證未定義 net、截斷檔、cycle 和不支援 gate 遇到時的硬錯誤。用獨立的小型 BENCH 解譯器或明列真值表比對轉換後每一個 PO，覆蓋 NAND／XOR 等實際支援型別、重排 PI／PO 與 DFF cut 正例。
3. 目前的 `#wire` 可稱 `aig_wires`，為轉換後的 AND receiving pins。`original_wires` 統計應固定定義是否計入 PO port 和 latch edges，並在報告中指出論文 `#wire` 的來源細節有缺口。提供原始 pin 至 AIG 實體 pin 的一對一、一對多或不可直接對應的標記。測試數位順序與 source-edge 合併時，同名 net／不同 pin 不能誤合併。每個 case 同列 `paper_wire_count` 與 `original_wires`／`aig_wires`，紙本的科學記號以其顯示精度保留。例如 `aspdac_l2_resume/summary.tsv` 的 C3540 原始 2,939 條，與論文 `#wire` 數字相同，轉換後有 2,540 個 AIG receiving pins。`#wire` 單項相同仍不足以證明拓樸同一。
4. 工程 scope `all` 保留接收 gate 以重現 g7 gate 範例。Table scope `paper` 排除接收 gate 當 attachment。當多輸入 gate 被拆成 AIG，原 gate 的直接接收位置和合成中間 node 可能不一樣。AIG 現行範圍列為 `aig` profile。若有完整原 gate mapping／原始拓樸，才可以添加 source-level profile，在 topology 字段明確辨識。

## 8. R7｜Table I／II 和 Figure 7 的驗收與效能路徑

| 交付 | 實作要求 |
|---|---|
| Table I | `wire`／`l0`／`l1`／`l2`。同個 build、輸入及 scope；每個 mode 列 #tar、#alt、covered pins 和狀態。原文表列 `--` 留作原報告缺值，自己的部分結果附原因。 |
| Table II | `wire`／`gate`／`gate_nolearn`／`gate_nofilter`。最後兩欄分別關閉 conflict learning 或 implication filter，其他輸入及設定一致。並列全量與任意抽樣驗證的覆蓋。 `sub` 與 `all` 的結果另列。 |
| Figure 7 | 同一 Release build、同次環境、分開記錄 discovery／verification／import／shard overhead。對每條曲線使用 `wire/l0/l1/l2`，完整計時以 monotonic clock 及秒為單位。可用對數軸，timeout 點保留狀態和截尾原因。相同 case 及 scope 的比值才可比較。 |
| 解讀 | 論文在 Linux x64、Xeon 5130 2 GHz、16 GB 的 QuteSAT 系列環境。Windows AIG 的 mode 相對比較記為本實作結果。論文 12.6× 等數字留在 reference 欄，交代分母和來源。原拓樸、前處理或硬體不足時分別註明可比較的層次。 |

- `#tar` 是至少有一個候選的 distinct receiving wire，兩種 stuck value 皆可得候選時，該 pin 仍只計一次。`#alt` 用 canonical expression、pin、attachment 與 stuck value 的 key。也列可跨 polarity 去重的可選統計，只有在確定原文計數策略時才取為主要可比數。 `#T` 以實測 discovery seconds 為主要本地欄位，明列與 paper timing scope 的未知項。
- 先跑 Figure 1 全 mode 含 CEC。跑 11/12 或 12/12 case 四 pin probe，辨識輸入和 mode 錯誤。再由已完成的小型組跑整線全模式，再跑 `C3540 C7552 b14 b15 b17`，最後跑 `b18 b19`。已知 C7552 L2 只覆蓋部分，b18 `gate_nofilter` 仍受限。依 R5 續跑可重用既有且語意相同的完成 shards。確認大案能處理合法多位數計數與長時測試後再排長跑。
- 先用小型與中型代表 case profile，再挑最大瓶頸修補。至少記錄 fault setup、projection、BCP、reason graph、trial expansion、candidate canonicalization 和 CEC 花費。優先降低重複 work（前綴重用、affected cone 快取、每 trial 掃描新增 trail、避免反覆 clone），每次保留 oracle 差分與候選 key diff。若改 decision order、filter 或 budget，視作不同設定，不混入同一 runtime regression。
- R7 可交付「完成相同模式矩陣的 AIG 實驗」或「輸入／資源使部分點尚未完成」的精確狀態。驗收只有在每格 coverage、provenance、mode identity 都齊全時才稱全量。原論文數值精確重現另需資料版本、等價 source topology、同一枚舉策略和時間量測條件。

### 可直接執行的命令

從 EdaLab 根目錄執行。每次比較使用新輸出目錄，既有結果保留。下面的 `--case-timeout` 在分片模式適用每片。命令中的 smoke／試跑數字由本規格選定，供首次跑通和量測使用。

```powershell
$cm = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ct = Join-Path (Split-Path $cm) 'ctest.exe'
& $cm -S satrar-cpp -B satrar-cpp/build-msvc -G 'Visual Studio 17 2022' -A x64
& $cm --build satrar-cpp/build-msvc --config Debug
& $ct --test-dir satrar-cpp/build-msvc -C Debug --output-on-failure
& $cm --build satrar-cpp/build-msvc --config Release
& $ct --test-dir satrar-cpp/build-msvc -C Release --output-on-failure

python satrar-cpp/scripts/rar_batch.py --scope all --out satrar-cpp/results/spec_fig1
python satrar-cpp/scripts/rar_batch.py --scope all --shard-wires 4 --out satrar-cpp/results/spec_fig1_sharded
python satrar-cpp/scripts/rar_batch.py --manifest satrar-cpp/benchmarks/manifests/aspdac2009.json --wire-limit 4 --discover-only --out satrar-cpp/results/spec_smoke
python satrar-cpp/scripts/rar_batch.py --manifest satrar-cpp/benchmarks/manifests/aspdac2009.json --case C432 --case b01 --modes wire l0 l1 l2 gate gate_nolearn gate_nofilter --discover-only --summary-only --out satrar-cpp/results/spec_full_small
python satrar-cpp/scripts/rar_batch.py --manifest satrar-cpp/benchmarks/manifests/aspdac2009.json --case C7552 --modes l2 --discover-only --summary-only --shard-wires 250 --case-timeout 300 --out satrar-cpp/results/spec_full_c7552_l2
python satrar-cpp/scripts/rar_batch.py --manifest satrar-cpp/benchmarks/manifests/aspdac2009.json --case b19 --modes wire --fetch-missing --wire-limit 4 --discover-only --out satrar-cpp/results/spec_b19
```

可用 `satrar-cpp/build-msvc/Release/satrar.exe --shell` 單獨重播。`d` 為 AIG var 4，在本 fixture 的 CLI seed 用 `trial -4`。`rar_apply` 的候選 ID 由最近一次搜尋輸出取得。

```text
read satrar-cpp/tests/fixtures/paper/fig1.aag
fault_ma pin 13 0 sa1 exact
fault_ma dom 13 sa0 trial -4 exact
rar_verify 13 0 sa1 gate 10
rar_verify 13 0 sa1 wire 10
```

## 9. R8｜有證據才啟動的涵蓋更新

1. **安全 source 涵蓋。** 論文 T1、T2 結構前提使用 `g_d` fanout 排除。現行 `eligible` 排除 target receiver 的整個 fanout cone，較保守，足以保持 source 在 target fault 的 good／faulty 版本同值。可對不在 `g_d` fanout、但在 target receiver cone 的漏失候選做獨立研究。先做 cycle 檢查，再用 good/faulty source-equality 精確查詢或直接對該候選做 R1 兩段 CEC。若候選僅由後者成立，標 `exact_verified_extension`，保留到擴充模式且個別計數。完整 T1–T3 provenance 仍需可重播 implication proof。至少提供一個可擴展漏失案例和一個移除不等價反例。
2. **原拓樸對照。** 確認具有論文用 benchmark 的資料版本、任一組已知 gate／wire count 和轉換規則後，才考慮導入保留多輸入 AND／OR／NAND／NOR 的原拓樸 profile。每個 propagation rule 需有真值表、controlling/non-controlling polarity、故障 injection、postdominator 和 two-stage CEC。保持相同 mode 和 edge counting 口徑。無法證明來源一致時把工作停在 R6 可追溯的 AIG profile。
3. **較深決策或策略。** §IV 允許依 arrival time 或 physical proximity 排序。若有時序或座標資料，先留 deterministic baseline，再用獨立 profile 測比較。Set order、depth、budget、物理資料版本全數記錄。原文未公布的 heuristics 使用現有可重跑設定，留在本地只作效能／涵蓋選項。

## 10. Claude／Cursor 的交付清單

- 每階段先讀錨點並重跑相關既有測試，提交最小修補，補能攔住該缺口的回歸，再跑完整 Debug／Release。每個 theorem 單元測試至少核對 signed candidate key、proof kind 和兩段 CEC，其中 fixture 另用獨立真值 oracle。R2 比對 BCP 之於 `F` 的 soundness，R3 比對 learned rule 全部前提，R4 比對兩側 separation。
- round-trip、測試 console、trace 命令、benchmark metadata、輸入來源和結果 TSV 存放位置要齊全。若遇到 blocker，記錄最小重現、錯誤與已完成的 coverage，繼續其餘獨立工作。更新 `docs/source-map.md`、`docs/experiment-protocol.md`、`results/PHASE_STATUS.md` 中與實作直接相關的段落。
- 最終回報用 `R# / 論文錨點 / 改動 / 命令與實測 / 未驗證條件`。將「論文定理在 fixture 與小 AIG 經驗證」、「實際完整探索的 benchmark cells」、「與原論文的數值可比性」各自附上證據。禁止用增大 budget、表格填值、改 scope 或 CEC 接受率掩蓋零覆蓋和失敗狀態。
