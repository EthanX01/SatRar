# SatRAR 核心一致性稽核與更新計畫

**日期：** 2026-11-24 · **工作區：** EdaLab · **專案：** `satrar-cpp` · **範圍：** 組合式 ASCII AIGER

**本輪範圍：** 稽核並規劃；產品原始碼未因本文件而修改。實作依下方 Phase 16A 起逐階執行。

---

## 1. 判定

現有專案**尚未達到** SatRAR 核心語意的一致性。ASCII AAG、結構模擬、MiniSat BCP 介面、SAT miter 與改線 CLI 已存在；從故障測試產生 MA、根據下游 dominator 生成候選、再證明加入與移除各自等價的整條路徑，仍有會**漏掉論文候選**和**誤判等價**的缺陷。

1. **先修等價驗證** — miter 常數編碼、PI 對接會造成假等價／假不等價。
2. **再修 fault、dominator、MA** — FaultContext 與 `dominatorsForWire` 語意錯誤。
3. **最後更新搜尋與實驗** — Figure 1 五模式 0 候選；L1/L2 與論文定義不符（應為 recursive learning 深度，非 cone 掃描）。

**對照文件：** `docs/Cursor_Composer_SatRAR_Plan_v2.md`、`docs/SatRAR_Paper_Level_Architecture_Plan.md`（已依本次稽核同步 §6 更正）。

---

## 2. 論文與講義定義（判定依據）

| 核心 | 本次判定要求 | 依據 |
|------|----------------|------|
| Fault test、MA | Pin s-a-b：故障版固定 pin 為 b；良好版該 pin 為 1−b 才激發；可偵測需 PO 變化 | 論文 §II.A Fig.1 p.191；講義 11 p.7–8, 19–21 |
| 2-Way RAR | 比較 MA(w_t) 與 MA(g_d)；共同變數相反 → 候選；L0 直接推論，L1/L2 為**遞迴學習深度** | §II.C Fig.2 Ex.4 p.192；講義 11 p.23–30；講義 07 p.49–52 |
| SRAR_Wire | Fig.5：反向 MA → MA(w_t)−MA(g_d) 差集試探 → 衝突支持反值，接到 **g_d** | §III.A Ex.5 Fig.5 p.193–195；講義 11 p.32–36 |
| SRAR_Gate | 決策導出與 target MA 相反 → 合取；Cor.2.1/2.2；g_d 控制／非控制決定極性 | §III.B Ex.6 pp.193–194；講義 11 p.37–39 |
| Fig.5/6 | Context 回溯、切換 g_d 共用條件、trace | §IV p.195；講義 06/07 |
| 實驗 | #wire/#tar/#alt/#T 口徑；排除 target 直接接收 gate | §V Table I/II Fig.7 pp.195–196 |

**Theorem 2：** 實作判準以 **MA(g_d) ⇒ ¬g_n** 為準（式 (2) 排版筆誤）；反向 implication 須納入 target MA 上的 literal。

---

## 3. 逐模組稽核摘要

| 優先 | 議題 | 程式證據（相對 `satrar-cpp/`） |
|------|------|-------------------------------|
| **P0** | 常數 CNF：AAG 0 在 SAT 中成真 | `src/sat/CnfEncoder.cpp`, `src/sat/MiterBuilder.cpp` |
| **P0** | Miter PI 依 AAG var 配對非 PI 順序 | `MiterBuilder.cpp`, `EquivalenceChecker.cpp` |
| **P0** | 改線無 golden/added/removed 中間態 | `Alternative.hpp`, `CircuitRewriter.cpp`, `Rewriter.cpp` |
| **P0** | Verifier 狀態／32-bit CaDiCaL／tempfile | `CadicalBackend.cpp`, `RarVerifier.cpp` |
| **P0** | Fault 假設方向、缺 propagation | `FaultContext.cpp`, `SatRarEngine.cpp` |
| **P0** | Dominator = fanin cone 非 postdominator；cycle 反向 | `GraphAnalysis.cpp`, `test_graph.cpp` |
| **P0** | L0/L1/L2 = 錯誤啟發式，非 MA(w_t) vs MA(g_d) | `SatRarEngine.cpp` |
| **P1** | SRAR_Wire / SRAR_Gate 未實作 Fig.5/6 | 同上 |
| **P1** | 無 incremental MA；learning 未實作 | `FaultContext.cpp`, `MinisatPropagationEngine.cpp` |
| **P2** | Batch／symbol／rar_verify 覆蓋不足 | `CommandShell.cpp`, `rar_batch.ps1`, AagParser/Writer |

完整表格與實測矩陣見使用者稽核原文 §3–§4（Figure 1 窮舉、手建三條候選均通過兩段比較但引擎未產出）。

---

## 4. 里程碑

| 里程碑 | 內容 |
|--------|------|
| **M1** | 等價與 fault oracle 正確（Phase 16A–17） |
| **M2** | Figure 1/5/6 + 講義案例可搜尋、可安全套用（Phase 18–20） |
| **M3** | Learning、消融、Table I/II 可重現（Phase 21–23） |

**順序：** 先 **CNF/MiniSat 語意重現 M2**，再 native gate BCP（效能）。**優先於**舊架構計畫「先做 trace/native BCP」。

---

## 5. 更新 Phase 路線（取代舊 16–24 部分編號）

| Phase | 名稱 | 優先 |
|-------|------|------|
| **16A** | CNF、miter、CEC 狀態機、四則 regression、並行 tempfile | P0 — **下一個 Cursor 任務** |
| **16B** | x64 CaDiCaL／backend 可追溯、整合測試不再假 PASS | P0 |
| **17** | Postdominator、cycle、good/faulty miter、exact MA oracle | P0 · M1 |
| **18** | 可回溯 MA、Fig.6 trace、`MaResult` 型別 | P0 |
| **19** | 2WRAR L0 + SRAR_Wire（MA(w_t) vs MA(g_d)） | P1 |
| **20** | add/remove 三態 + SRAR_Gate | P1 · M2 |
| **21** | L1/L2 recursive learning、conflict learning、filter | P1 |
| **22** | Native gate BCP、Theorem 3 | P2 |
| **23** | Table I/II batch、Figure 7 | P2 · M3 |

---

## 6. 下一個 Cursor 任務（16A 驗收清單）

1. 四個 regression：**x∧0** CNF、**0 vs 1** miter、**PI 重編號等價**、**PI 順序不等價**。
2. 修正 `CnfEncoder`、`MiterBuilder`、`EquivalenceChecker` 常數與 **piMap（預設第 i 個 PI 共用 SAT var）**。
3. 驗證狀態：`SAT|UNSAT|Unknown|Timeout|BackendError|InvalidInput`；僅兩段可信 **UNSAT** → accepted。
4. Miter **唯一 tempfile**；雙並行 CEC 測試。
5. 不依賴壞掉的 32-bit CLI 亦可跑 **in-process / PySAT 對照** regression。

**暫不改**候選搜尋邏輯（保留診斷）。

---

## 7. 重跑指令與 fixture

```powershell
$cm = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ct = Join-Path (Split-Path $cm) 'ctest.exe'
$auditBuild = '.\tmp\satrar_audit_20261124\build'
& $cm -S .\satrar-cpp -B $auditBuild -G 'Visual Studio 17 2022' -A x64
& $cm --build $auditBuild --config Release -- /m:1 /nodeReuse:false
& $ct --test-dir $auditBuild -C Release --output-on-failure
```

**Figure 1 fixture：** `tests/fixtures/paper/fig1.aag`（見該檔註解與 `docs/audit-rerun.md`）。

核心觀察命令：

```text
read tests/fixtures/paper/fig1.aag
fault_ma pin 13 0 sa1
fault_ma dom 15 sa0
fault_ma dom 13 sa0
rar_wire 13 0 sa1
rar_gate 13 0 sa1
```

---

## 8. 來源

- ASP-DAC 2009 SatRAR（工作區 `ASPDAC-2009-SatRAR.pdf`）
- 講義 06 / 07 / 11
- `docs/Cursor_Composer_SatRAR_Plan_v2.md` §3、§5–6