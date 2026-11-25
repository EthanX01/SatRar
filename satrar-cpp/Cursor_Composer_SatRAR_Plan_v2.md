# SatRAR C++｜Cursor Composer 實作計畫

把本檔交給 Cursor Composer 作為專案指令。從 **Phase 0R** 開始，每次只執行指定的 phase。完成當期驗收後，回報檔案、實測指令、結果和下一期工作，再停在該 phase。

## 1. 專案與範圍

你是我的 C++ 研究專案開發助手。請逐物件實作 Chi-An Wu、Ting-Hao Lin、Shao-Lun Huang、Chung-Yang Huang 的 ASP-DAC 2009 論文《SAT-Controlled Redundancy Addition and Removal — A Novel Circuit Restructuring Technique》。

- 工作區是 `C:\Users\User\Documents\file\NTU\EdaLab`。專案根目錄是已存在的 `satrar-cpp\`。先讀工作區 `AGENTS.md`、專案 `README.md`、`CMakeLists.txt`、`app/main.cpp`、`docs/source-map.md` 和 `third_party/README.md`。
- 現有 Phase 0 提供 `satrar --help`、`--version`。在原專案上延續。工作區的 `20261124_SATRAR\` 是另一個目錄。上層 `minisat-solver\` 是唯讀來源，任何相容性修改都放在專案的副本或 patch。
- 完成後應能讀取、檢查、模擬與匯出組合電路 `.aag`，分析指定接腳的 SatRAR 候選，驗證並套用改線，最後以 batch 重現論文形式的比較。
- 以 C++17 和 CMake 開發。使用小型值型別與組合。每一期只加入正在使用的物件。透過可執行範例或測試驗收，再繼續下一個物件。

## 2. 2026-11-24 實測環境

| 項目 | 本工作階段的結果 |
| --- | --- |
| Windows native 建置 | 已找到 Visual Studio Build Tools 2022 17.12、MSVC 19.42.34433、Windows SDK 10.0.22621.0。以 VS 2022 x64 generator 重新 configure 並完成 Debug build，新的 exe 可執行 `--help` 和 `--version`。 |
| CMake | 一般 PowerShell 的 PATH 未找到 `cmake`。Visual Studio 內附的 CMake 3.29.5-msvc4 可用，以下用絕對路徑。 |
| 舊 MinGW | `C:\MinGW\bin\g++.exe`、`mingw32-make.exe` 都存在，這次呼叫 `--version` 時都遇到 DLL 載入錯誤。README 原先的 MinGW 建議需更新。 |
| Phase 0 既有問題 | MSVC 回報 C4819（來源字元編碼）與 C4702（不可達程式碼）。CLI 仍能執行。README 和 help 需一起更新作者與雙 solver 分工。 |
| 本地 MiniSat core | 直接編譯缺 `zlib.h`。補上標頭後，MSVC 對 `Solver.cc` 末段的 `PRIu64` 相鄰字串回報 C3688。在暫存副本調整字串和巨集之間的空白，搭配工作區已有的薄相容標頭後，已通過 C++17、直接 BCP、reason、level 與回溯 smoke test。 |
| CaDiCaL | 目前未找到可直接執行的版本，尚未做建置或求解驗收。WSL 查詢在這個執行環境回報 `Wsl/E_ACCESSDENIED`。 |

### 本機建置命令

從 EdaLab 根目錄執行。這是 VS 多組態建置，使用 `--config Debug` 或 `--config Release`。不同 toolchain 使用不同 build directory。

```powershell
$vs = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake --version
& $cmake -S .\satrar-cpp -B .\satrar-cpp\build-msvc -G 'Visual Studio 17 2022' -A x64
& $cmake --build .\satrar-cpp\build-msvc --config Debug
& .\satrar-cpp\build-msvc\Debug\satrar.exe --help
& .\satrar-cpp\build-msvc\Debug\satrar.exe --version
```

若 VS 安裝位置有變，用 `vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath` 找到 Build Tools。使用 `/utf-8` 修正 MSVC 的編譯字元設定。CLI v1 用 ASCII 命令與訊息，避免終端字碼影響文字驗收。

## 3. 雙 solver 分工

| 元件 | 輸入 | 責任與輸出 |
| --- | --- | --- |
| `MinisatPropagationEngine` | 原始電路的 gate CNF，及當前情境的 assumptions 和 decisions | 由 SatRAR 控制 push level、BCP、checkpoint 與 rollback。提供 assignment、level、reason、conflict contributors。 |
| `CadicalBackend` | CaDiCaL 驗證查詢的 miter CNF | 完整 SAT 搜尋。回傳 `Sat`、`Unsat` 或 `Unknown`。有 SAT 時取出 PI 反例。將執行失敗與求解結果分開回報。 |

- **MiniSat 整合**。先用本地 `minisat-solver/minisat/core/Solver.cc`、`utils/Options.cc`、`utils/System.cc` 建靜態 core target。試做範圍為 in-process CNF，不需編 MiniSat 的 `Main.cc` 或讀 gzip DIMACS。若需薄相容標頭，參考 `../Lazy_SAT_project/tetris_lazy_sat/compat/zlib.h`，把 include 限定在 MiniSat core target 並保留 license。若使用 gzip parser，要改接真正的 zlib。相容性 patch 只改專案副本，記錄來源、版本與檔案雜湊或 commit。
- **CaDiCaL 整合**。第一個接受的 Windows 介面可以是外部 `cadical.exe`，透過明確的路徑設定、DIMACS 暫存檔和 process exit code 呼叫。驗收可用的 Windows x64 binary，記錄來源、版本、平台及 checksum。也可在已驗證的 MSYS2 UCRT64 或 Linux 環境，用 pin 住的版本進行建置。若採用 C++ library，主程式和 solver library 要有相容的 compiler、架構與 runtime。保留可用同一套 toolchain 重建專案的方式。
- **驗證 backend 是可選依賴**。先完成 parser、命令列與候選搜尋。CaDiCaL 未配置或啟動失敗時，驗證命令直接回報狀態，候選維持 `unverified`。小電路可用窮舉當開發參考，正式 SAT 驗證由 CaDiCaL 執行。核心 build 支援離線，不在 configure 時偷偷下載 solver。
- **兩個 MA 是同一個 MiniSat 的兩個情境**。保存 `MA(w_t)` 的值快照，回溯同一 instance 後建立 `MA(g_d)`。兩者可共同使用原始 gate CNF。CaDiCaL 的 miter 用獨立的 CNF 和變數映射。

## 4. 目標目錄

```text
satrar-cpp/
  CMakeLists.txt
  README.md
  app/main.cpp
  include/satrar/
    aig/        Literal, Node, WireRef, Circuit, AagParser, AagWriter
    analysis/   GraphAnalysis, Fault, FaultContext, MandatoryAssignments
    sat/        CnfFormula, CnfEncoder, MinisatPropagationEngine,
                SatBackend, CadicalBackend, MiterBuilder
    rar/        Alternative, SatRarEngine, Rewriter, EquivalenceChecker
    app/        CommandShell
  src/          依 include/satrar 分目錄，放相應的 .cpp
  docs/         Cursor_Composer_SatRAR_Plan_v2.md, source-map.md,
                algorithm-notes.md, experiment-protocol.md
  tests/        fixtures/ 與 CTest 測試
  benchmarks/   manifests/ 和 inputs/
  scripts/      benchmark runner 及報表輸出
  third_party/  README.md、MiniSat 來源資訊、license 與相容性 patch
  results/      CSV、設定與執行環境紀錄
  build-msvc/   產出目錄，可由 CMake 重建
```

## 5. 逐物件開發順序

| Phase | 本期主要物件 | 實作與驗收 |
| --- | --- | --- |
| **0R** | 現有 bootstrap | 用上述命令重建，更新 README、source map、help 與依賴說明。修正現有 warning 與作者名稱。確認 `--help`、`--version`、未知選項的 exit code。 |
| **1** | `Literal` | AAG 用 `2*variable+sign`。常數為 0/1，反相切換最低位。和 DIMACS signed literal 分型，驗證 0↔1 和 2↔3。 |
| **2** | `Node`、`WireRef` | 保存常數、PI、AND。反相附在 literal 上。用 `{destinationNode, inputPin}` 定位支線，來源相同但 pin 不同仍是兩條 wire。 |
| **3** | `Circuit` | 保存原 AAG ID、PI/PO 順序、fanin/fanout、拓撲順序。驗證缺少定義、重複 ID 和環路。用手建電路驗收模擬。 |
| **4** | `AagParser` | 組合 ASCII AIGER，處理 symbol、comment、稀疏 ID 與 forward reference。讀完定義再拓撲排序。第一版明確報告 latch、二進位 AIG 與不支援的非零 property 欄位。驗收官方 AND、半加器四組輸入與壞檔行號。 |
| **5** | `CommandShell` | 提供 `read`、`stats`、`inputs`、`outputs`、`gate`、`fanin`、`fanout`、`sim`、`help`、`quit`。檔名可包含空格。命令明確區分 node ID 和 raw AAG literal，讀檔失敗時保留先前載入的電路。 |
| **6** | `CnfFormula`、`CnfEncoder` | AND 的 Tseitin 編碼、constant 和反相、AIG/DIMACS 對照、DIMACS writer。加 `write_cnf` 命令。用小電路全輸入模擬交叉檢查。 |
| **7** | `MinisatPropagationEngine` | 先把環境測試做成可重跑的 core build。以薄 adapter 提供 `assume`、`propagate`、`checkpoint`、`rollback`、`reasonOf`。測試立即矛盾、連鎖推論、回溯後 assignment 消失、根層 clause 保留。 |
| **8** | `GraphAnalysis` | fanout cone、支線到所有 PO 的 dominator、加線前 cycle check。測試有分岔、匯合、兩個 PO 或無法到達 PO 的電路。 |
| **9** | `Fault`、`FaultContext`、`MandatoryAssignments` | 分開 target receiving pin 和 dominator output fault。建立 fault excitation 和可證明必要的 propagation assumptions，對照 Figure 1/6。測試反相 pin、多 fanout、AND/OR 對應。 |
| **10** | `Alternative`、`SatRarEngine` | 先做 `2WRAR_L0`，再做 `SRAR_Wire`、`SRAR_Gate`。接著加入 learning 與 implication filter 的開關。保存候選極性、接入點及推論證據。 |
| **11** | `SatBackend`、`CadicalBackend` | 在 Windows 跑 SAT 和 UNSAT 的小 DIMACS smoke test。CLI 模式用明確參數啟動程式，為每次查詢使用獨立暫存檔。處理 10/20、timeout、缺少執行檔、異常結束與模型解析。若使用 library，驗收 assumptions、solve、model。 |
| **12** | `MiterBuilder`、`EquivalenceChecker` | 驗證 fault 及改線，兩份電路共用 PI，內部變數分開。各 PO 做 XOR，再要求至少一個不同。SAT 反例用 `Circuit::evaluate()` 回放。獨立測試多 PO、真的等價和故意不等價。 |
| **13** | `Rewriter`、`AagWriter` | 建只加候選版與再移除 target 版。保留 pin 定位到改線完成後再清理 dead logic。驗證通過才標記可套用，寫出 AAG 後重新載入並檢查等價。 |
| **14** | Batch runner | 先跑 paper fixture 與 EPFL 開發集，再跑 ISCAS85/ITC99。輸出 `#wire`、`#tar`、`#alt`、`#T`、驗證與 timeout。可用 CLI 重跑同一組參數。 |
| **15** | 進階複現 | 以相同引擎加入 `2WRAR_L1/L2` 和 recursive learning。若需要更貼近原論文的性能，再引入 `CircuitPropagator`。Section III Theorem 3 的 alternative sub-circuit 可作後續擴充。 |

## 6. SatRAR 核心實作契約

1. **推論與完整搜尋**。MiniSat 的 BCP 保存的是已推出的必要賦值。BCP 無衝突表示尚未推出矛盾。完整 fault test 交由 CaDiCaL 的 good/faulty miter。驗證與發現候選各記時間。
2. **Figure 5 的回溯**。將論文的每個 context level 映射到實際 checkpoint。MiniSat 的真正 level 0 保留全域 facts。論文 Figure 6 的 `f=1` 雖標成 level 0，仍要建成可撤銷的 fault assumption。換 dominator 時保留下游共同條件，離開 target 時清掉所有 fault assumptions。
3. **Candidates**。直接相反的 MA 產生 wire。額外 decisions 導致反向推論時，將產生該推論的 decisions 與對應的 target MA literal 一同用於建 gate。導致 conflict 時，只收集 reason graph 可回溯到的候選 decisions。跟 dominator 有相依或可能成環的候選一律檢查。依故障位置、gate 類型和 complemented edge 決定接線極性。
4. **多輸入與多路徑**。Side input 只有在其 non-controlling 值對觀察故障確實必要時才能作為假設。對 reconvergent fanout，不可以把另一條可能帶有故障差異的路徑當成固定 side input。無法從支援的結構證明必要性時，標記該情境並以 exact fault test 檢查，避免把過強的情境當成整個 fault test。
5. **Learning**。若衝突依賴 fault context (D) 和候選 decisions (G)，回推出來的 clause 要保留實際用到的 context 依賴。加入 MiniSat 的永久 clauses 必須由基底 CNF 蘊含。Context 內局部的推論隨回溯失效。測試同一 circuit 下兩個 assumptions 互相衝突的情境。
6. **驗證與回歸**。Figure 1 的 `g6 -> g7` 應能找到論文的 `g5 -> g9` 案例，手建 fixture 時保留 paper gate 對照及全部 PO。Figure 6 用 trace 驗證共享條件、回溯與反向 implication。先驗證「原版 vs. 只加候選」，再驗證「只加候選 vs. 移除 target」。只有兩者皆 UNSAT 才接受。候選結果另保留 `unverified`、`rejected` 與理由。
7. **實驗口徑**。實作 `SRAR_Wire`、`SRAR_Gate`、`SRAR_Gate-Lrn`、`SRAR_Gate-Imp`、`2WRAR_L0/L1/L2`。以選項排除接到 target 直接接收 gate 的候選來對照 Section V，Figure 6 教學測試可開啟。逐一記錄輸入雜湊、轉換、是否切 latch、計數單位、去重、模式、solver 版本、Release build、timeout。若原始 gate netlist 被拆成 AIG，要記錄拓撲與 target 計數差異。表格分別呈現搜尋 runtime、驗證 runtime 和 verified counts。報告將 CNF MiniSat 的數字標為此實作結果。

## 7. 參考資料與要讀的部分

| 來源 | 讀哪些部分及用途 |
| --- | --- |
| `../ASPDAC-2009-SatRAR.pdf` | Section II 的 fault、MA、2WRAR。Section III 的 Theorem 1–3、Corollary 2.1/2.2。Section IV Figure 5/6 的共享、reason、filter。Section V Table I/II 與 Figure 7 的比較方法。 |
| `../06. Introduction to SAT.pdf`、`../07.  Advanced SAT Techniques.pdf`、`../11. Applications of SAT on Logic Syn.pdf` | SAT、BCP、CDCL、Tseitin、logic implication、ATPG 與 RAR 教學。 |
| [AIGER FORMAT](https://github.com/arminbiere/aiger/blob/master/FORMAT) 與 [官方 repository](https://github.com/arminbiere/aiger) | ASCII header、literal、PI/PO/AND 順序及 symbol。用 `examples/and.aag`、`examples/halfadder.aag`、`aigtoaig`、`aigsim` 和 `aigmiter` 做輸入與交叉檢查。 |
| `../minisat-solver/minisat/core/Solver.h`、`Solver.cc` | Adapter 對照 `newDecisionLevel`、`enqueue`、`propagate`、`reason`、`cancelUntil`、`analyze`。閱讀 `implies` 的返回值及清除 trail 行為。 |
| [CaDiCaL C++ API](https://github.com/arminbiere/cadical/blob/master/src/cadical.hpp)、[建置文件](https://github.com/arminbiere/cadical/blob/master/BUILD.md)、[CLI 來源](https://github.com/arminbiere/cadical/blob/master/src/cadical.cpp) | 驗證端的 `assume`、`solve`、`val`、退出狀態、命令列模型輸出與建置限制。用部署版本的 tag 核對 API。 |
| [CMake VS 2022 generator](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2017%202022.html)、[MSYS2 environments](https://www.msys2.org/docs/environments/)、[STP 的 Windows 建置說明](https://github.com/stp/stp/blob/master/docs/building.rst) | VS x64 build 的用法。MSYS2 UCRT64 是 CaDiCaL 待驗證的建置路徑之一。 |
| [2-Way RAR, EURO-DAC 1996](https://cecs.uci.edu/~papers/compendium94-03/papers/1996/eurdac96/pdffiles/d18_3.pdf)、[Recursive Learning, TCAD 1994](https://www.eecg.toronto.edu/~veneris/kunz1.pdf) | 2-way 判準與 recursive-learning baseline。 |
| [QuteSAT, DATE 2007](https://past.date-conference.com/proceedings-archive/2007/DATE07/PDFFILES/09.4_2.PDF) | 讀 circuit-based BCP、gate watching 與 implicit implication graph，作為進階電路推論器參考。 |
| [mockturtle](https://github.com/lsils/mockturtle)、[lorina](https://github.com/lsils/lorina)、[Berkeley ABC](https://github.com/berkeley-abc/abc) | 參考 AIG 的 C++ 資料結構、AIGER parser API 與 CEC。 |
| [EPFL](https://github.com/lsils/benchmarks)、[ISCAS85](https://pld.ttu.ee/tt/benchmarks/iscas_85/)、[ITC99](https://www.cad.polito.it/tools/itc99.html) | EPFL 用於開發壓測。ISCAS85/ITC99 用於對照論文。先確認檔案版本、格式與前處理。 |

## 8. Cursor 現在的任務

**只執行 Phase 0R。**保留現有 Phase 0 CLI，更新環境文件與錯誤的 solver 描述，修正 Phase 0 編譯警告。使用原生 Windows MSVC x64 路徑，建到獨立 `build-msvc`，執行 help、version 及未知選項驗收。回報實際檔案和命令、結果，以及 Phase 1 的預計介面。完成後停止，等我指定 Phase 1。

