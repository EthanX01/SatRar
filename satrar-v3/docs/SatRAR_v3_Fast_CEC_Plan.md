# SatRAR v3 快速等價驗證計畫

2026-11-27 · EdaLab · 修改專案 `satrar-v3/`（沿用 `satrar-cpp/` 的搜尋與 Rewriter）

## 0. 結論先講

**ABC 不是瓶頸，瓶頸是 satrar 自己「把候選電路做出來並寫成檔案」。** 在 b15 上實測：

| 步驟（b15，8,877 AND、519 PO） | 每個候選的成本 |
|---|---|
| 改線＋寫兩個 `.aig`（`rar_dump`，200 候選 63.2 s） | **約 310 ms** |
| ABC 兩段 `&cec`（400 jobs 一個 process 6.1 s） | 約 30 ms |
| ABC e2e `&cec`（200 jobs 2.0 s） | **約 10 ms** |

37,623 候選 × 310 ms ≈ 11,700 CPU 秒，和 b15 逾時那次 satrar.exe 實際用掉的 11,675 CPU 秒幾乎一樣，而 yosys-abc 每個 process 都不到 1 秒。所以要快，順序是：

1. **改用 e2e**（N0≡N2，一個候選一個 ABC job）：不用改程式。
2. **重寫「產生候選電路」這段**：一次線性掃描，不 clone、不重建、不用 hash set。目標從 310 ms 降到 ≤ 15 ms。
3. **只送受影響的輸出錐給 ABC**：b15 平均只要 39% 的 AND，ABC 與寫檔再省約 2.5 倍。
4. 多開 worker、分片保存結果，讓 b14、b17 也跑得完。

ABC 仍是**唯一**的證明引擎。下面每個加速都只改「送什麼給 ABC」與「怎麼做出來」，不加入任何自己的等價證明。

## 1. 工作基線

| 資料 | 內容 |
|---|---|
| `satrar-v3/results/cec_abc_c432`、`cec_abc_more`、`cec_abc_small` | 現行 v3（兩段 CEC、4 jobs）：C432 1112/1112 5.7 s、C499 698/698 4.7 s、C3540 4804/4804 38 s、C7552 15803/15803 208 s、b01 58/58、b13 336/336、C6288 0 候選。全部 Accepted，0 rejected、0 timeout、0 mismatch。**這是正確性對照組。** |
| `satrar-v3/results/cec_abc_b15` | b15 兩段 CEC，`--case-timeout 3600` 逾時，沒有 summary。 |
| `satrar-cpp/results/aspdac_wire_20261126/summary.tsv` | 只做 discovery：b14 135,384 候選 134 s；b15 37,623 候選 142 s。 |
| `satrar-cpp/results/aspdac_wire_b17/summary.tsv` | b17 123,243 候選，discovery 562 s。 |
| `%TEMP%\satrar-b15-dump` | b15 前 200 個候選的 `golden.aig`、`cand_k_n1.aig`、`cand_k_n2.aig`，加上 `bench.abc`、`e2e.abc` 計時腳本。 |

### 熱點（皆在 `satrar-cpp/src`，v3 直接編譯這些檔）

每個候選目前會跑一次 `Rewriter::removeTargetAfterWire`，兩段模式另外再寫 N1：

| 位置 | 做了什麼 | b15 的量 |
|---|---|---|
| `rar/Rewriter.cpp` `addAlternative`（第 79–125 行） | `circuit.clone()`（含 `rebuildFaninFanout`）；新建 `GraphAnalysis` 重算整顆電路的 postdominator 樹；每個 operand 再做一次 `fanoutCone`（`unordered_set`）檢查 cycle；`substituteAigVar` 掃全部節點後又 `rebuildFaninFanout`；最後 `validate()`。 | 好幾次 O(N) 加 hash |
| `rar/Rewriter.cpp` `removeTarget`（第 127–133 行） | 再 `clone()`；`replaceAndFanin` 又 `rebuildFaninFanout`；`simplify(simplify(...))` 跑兩次。 | |
| `rar/Rewriter.cpp` `simplify`（第 23–75 行） | **對每個 PO 各做一次 `faninCone`，每次新建一個 `unordered_set`**，再合併進 `live`；接著 `topologicalOrder()`、`unordered_map` 查表、`validate()`。 | 519 個 cone，總大小 629,712，跑兩次，等於每候選約 250 萬次 hash 插入 |
| `v3/src/aig/AigWriter.cpp` 第 38、63 行 | 每次寫檔都 `validate()` 再 `topologicalOrder()`（每次重建 adjacency 的 `vector<vector>`）。 | 兩段模式每候選寫 2 檔 |
| `aig/Circuit.cpp` `topologicalOrder`（第 260 行） | 每次呼叫都配置 N 個 `vector`。 | 每候選呼叫 ≥ 5 次 |

搜尋階段已經驗證過 dominator 與無 cycle，驗證階段再算一次是重複工作。

## 2. 設計原則

- **ABC 是唯一的 prover。** 判定規則不變：Accepted 必須是 ABC 對該 job 恰好一次 `Networks are equivalent`；Rejected 必須是 ABC 給出反例，且反例在**完整的** N0 與 N2 上用 `Circuit::evaluate` 重播後 PO 不同；其他一律 Unverified。
- **Fast 和 reference 兩條路並存。** 現在的 `Rewriter` 保留為 `reference` builder；新的 `CandidateBuilder` 是 `fast` builder。用 CLI 切換，測試要求兩者輸出功能相同。
- **縮小錐只能刪掉「保證沒變」的 PO。** 判斷 PO 沒變靠建構時的結構追蹤（第 5 節），不靠模擬或猜測。若追蹤結果不可信，就退回送完整電路。
- **結果邊跑邊存。** 大電路一律分片，逾時只損失一片。

## 3. Phase 總覽

| Phase | 內容 | 改程式？ | 預期 b15 每候選成本 |
|---|---|---|---|
| P0 | 計時欄位與 `rar_bench` 命令，把時間拆成 build、write、ABC | 小 | 量測 |
| P1 | 大電路預設 e2e、多開 worker | 只改設定 | 約 250 ms |
| P2 | `GoldenContext` 加 `CandidateBuilder`（一次線性掃描產生 N2） | 中 | ≤ 10 ms |
| P3 | 由 dense 結構直接寫 AIGER，golden 每個 worker 只寫一次 | 小 | ≤ 3 ms |
| P4 | 只送受影響的輸出錐（affected-output cone） | 中 | ABC 約 10 ms 降到約 4 ms |
| P5 | 分片、續跑、進度列；跑 b14、b15、b17 | 腳本 | |
| P6（選做） | 一個 ABC job 一次驗多個候選；或把 libabc 嵌進 process | 大 | 只在 ABC 佔 > 70% 時才做 |

## 4. Phase 0：先量清楚

### 4.1 計時欄位

`BatchRarVerifier` 的每個 worker 累加三個 `std::atomic<uint64_t>`（單位 ns）：

```cpp
struct CecTiming {
    std::atomic<std::uint64_t> buildNs{0};   // 產生 N1/N2（或 CandidateBuilder）
    std::atomic<std::uint64_t> writeNs{0};   // 寫 .aig
    std::atomic<std::uint64_t> abcNs{0};     // runProcess 期間（含 parse）
    std::atomic<std::uint64_t> coneAnds{0};  // 送進 ABC 的 AND 總數（兩邊相加）
};
```

`AbcCec::checkBatch` 也要回傳 write 與 process 各自的時間，可以放進新的 `CecBatchStats` out-parameter。`RAR_SUMMARY` 追加：
`cec_build_seconds cec_write_seconds cec_abc_seconds cec_cone_ands_mean`（CPU 秒，各 worker 相加）。

### 4.2 `rar_bench <mode> <maxCandidates> [offset]`

不寫 log、不累積結果，只把前 `maxCandidates` 個候選丟進驗證 pipeline，最後印：

```
RAR_BENCH candidates=200 build_ms=... write_ms=... abc_ms=... accepted=200 builder=reference stages=two cone=off
```

**通過條件**：b15 `rar_bench wire 200` 在 reference 加兩段模式下，build+write ≈ 0.3 s/候選、ABC ≈ 0.03 s/候選，與第 0 節數字相符。之後每個 Phase 都用同一個命令比較前後。

## 5. Phase 1：設定層的加速（不改程式）

- **e2e 足以判定正確性。** RAR 要保證的是 N0≡N2；N1 只是中間產物。兩段模式只是在失敗時多告訴你「是加線錯還是刪線錯」。
- 大電路（AND > 3,000）預設 `--cec-stages e2e`，`--cec-jobs` 設成實體核心數，`--cec-batch 64`。
- 兩段模式保留給小電路與除錯：`rar_batch.py` 在 e2e 回報 Rejected 時，用兩段模式重跑那一個候選當診斷（`--diagnose-rejected`），結論仍以 e2e 為準。

**通過條件**：C432、C499、C3540、C7552 在 e2e 下 Accepted 數與基線相同。

## 6. Phase 2：`GoldenContext` 與 `CandidateBuilder`

### 6.1 新檔案

```
satrar-v3/include/satrar/rar/GoldenContext.hpp
satrar-v3/src/rar/GoldenContext.cpp
satrar-v3/include/satrar/rar/CandidateBuilder.hpp
satrar-v3/src/rar/CandidateBuilder.cpp
satrar-v3/include/satrar/aig/DenseAig.hpp      // 輕量 AIG 與寫檔、模擬
satrar-v3/src/aig/DenseAig.cpp
satrar-v3/tests/test_candidate_builder.cpp
```

### 6.2 `GoldenContext`：每顆 golden 只算一次

所有欄位用 dense index（0..N-1，依拓撲順序），不再用 `unordered_map`。

```cpp
namespace satrar::rar {

struct GoldenContext {
    // Dense numbering: 0 = const0, 1..I = PIs, I+1.. = ANDs in topological order.
    std::uint32_t numInputs = 0;
    std::vector<std::uint32_t> fanin0, fanin1;   // dense literals (2*idx + inv), ANDs only
    std::vector<std::uint32_t> outputs;          // dense literals
    std::vector<std::uint32_t> denseOfNode;      // aig::NodeId -> dense idx
    std::vector<aig::NodeId> nodeOfDense;
    std::vector<std::uint32_t> foStart, foList;  // CSR fanout (dense idx of AND readers)
    std::vector<std::uint32_t> poStart, poList;  // dense idx -> PO positions that read it

    static std::shared_ptr<const GoldenContext> build(const aig::Circuit& golden);
    std::uint32_t denseLit(aig::Literal lit) const;
};

}  // namespace satrar::rar
```

`build` 呼叫一次 `golden.topologicalOrder()` 和一次 `validate()`。之後所有候選共用這個 `shared_ptr<const>`，各 worker 只讀不寫。

### 6.3 `CandidateBuilder`：一次掃描產生 N2

語意必須和 `Rewriter::removeTargetAfterWire` 相同，也就是加入 alternative、把 target pin 綁成 stuck-at 常數、常數折疊，再清掉不可達節點。

```cpp
struct CandidateNetlist {
    aig::DenseAig n2;                    // 已折疊、已清除死節點
    std::vector<std::uint8_t> poDirty;   // 每個 PO：1 = 函數可能改變
    std::uint32_t dirtyAnds = 0;
};

class CandidateBuilder {
public:
    explicit CandidateBuilder(std::shared_ptr<const GoldenContext> golden);
    CandidateNetlist build(const Alternative& alt);   // 不做 dominator / cycle 檢查
private:
    std::shared_ptr<const GoldenContext> g_;
    // 每個 worker 一個 builder，以下暫存陣列重複使用，不重新配置
    std::vector<std::uint32_t> map_;     // golden dense idx -> N2 literal
    std::vector<std::uint8_t> dirty_;
    std::vector<std::uint8_t> live_;
};
```

`build` 的演算法，每一步都是 O(N)，只用 `vector`：

1. **前置**：把 alternative 的 `source`（或 conjunction/disjunction operands）、dominator `dom`、target `(dest, pin, stuckAt)` 轉成 dense index。新增的 AND（conjunction 鏈與 wrapper）在邏輯上排在 `dom` 之前。合法性在 discovery 階段已檢查過，這裡只在 Debug build `assert`。
2. **正向掃描**（依拓撲順序 `i = I+1 .. N-1`）：
   - `a = resolve(fanin0[i])`、`b = resolve(fanin1[i])`；若 `i == dest`，把 `pin` 那一邊換成常數 `stuckAt`。
   - 折疊規則與 `simplify` 完全一致：`a==1 || a==b` 時結果為 `b`；`b==1` 時結果為 `a`；`a==0 || b==0 || a==~b` 時結果為 0；否則建一個新 AND。
   - 若 `i == dom`：先算出 `i` 的值 `v`，建出 alternative 條件 `c`，`map_[i] = AND(v, c)`。
   - `dirty_[i] = (i == dest) || (i == dom) || a != identity(fanin0[i]) || b != identity(fanin1[i]) || 被折疊`。白話說：只要這個節點不是「用原本的兩個輸入原封不動建出來」，就是 dirty。
3. **PO**：`poDirty[k] = (map_[out] != identity(out))`，或 `out` 所在節點為 dirty。
4. **反向 liveness**：從所有 PO 出發，反向拓撲順序一次標記 `live_`。這取代原本 519 次 `faninCone` 加 hash set，以及第二次 `simplify`。
5. **輸出**：依拓撲順序收集 live 的 AND，重新編號成 `DenseAig`。

**為什麼不 dirty 的 PO 一定沒變**：以拓撲順序歸納。clean 節點的兩個輸入都是 golden 原本的訊號，而這些訊號本身也是 clean（否則 resolve 出來的 literal 會不同），所以 clean 節點在 N2 中的函數和在 golden 中相同。一個 PO 若指向 clean 節點且 literal 相同，函數也相同。這是建構方式保證的性質，不是另一個 prover。

### 6.4 不再需要的工作

- 不 `clone()`、不 `rebuildFaninFanout()`、不 `substituteAigVar` 全掃。
- 不重算 postdominator 與 `fanoutCone`，discovery 已檢查過。
- 不 `validate()`：DenseAig 依建構方式就是拓撲順序。Debug build 保留 `DenseAig::check()`。
- e2e 模式完全不產生 N1。

### 6.5 接到 `BatchRarVerifier`

```cpp
enum class CandidateBuilderKind { Reference, Fast };
struct BatchVerifyOptions {
    CecStages stages = CecStages::EndToEnd;
    CandidateBuilderKind builder = CandidateBuilderKind::Fast;
    bool coneOnly = false;          // Phase 4
    std::size_t auditEvery = 0;     // Phase 4：每 N 個候選做一次完整電路的 e2e
    std::size_t batchSize = 64;
    std::size_t workers = 1;
    int timeoutSeconds = 30;
};
```

- `Fast` 只支援 e2e，`TwoStage` 自動使用 `Reference`。
- 每個 worker 持有自己的 `CandidateBuilder`，共用一份 `GoldenContext`。
- `BatchRarVerifier` 建構時建一次 `GoldenContext`；若 `rar_batch` 在中途接受候選而換了 golden（per-wire accepted 是否會改變 golden，以 `CommandShell.cpp` `rar_batch` 現行行為為準），就換新的 context。

### 6.6 CLI 與腳本

- `set cec_builder fast|reference`（預設 fast）、`show cec` 顯示 builder。
- `rar_batch.py`：`--cec-builder {fast,reference}`，`SCHEMA_VERSION = 6`，fingerprint 加入 builder、cone、audit 設定。

### 6.7 測試（`test_candidate_builder.cpp`）

測試只檢查「fast builder 與 reference builder 產生相同的電路」，不負責證明 RAR 候選正確，那仍然是 ABC 的工作。

1. **小電路全窮舉**：fig1 與隨機 AIG（PI ≤ 12、AND ≤ 60，各 500 顆）。對每個 discovery 出來的候選，fast N2 與 `Rewriter::removeTargetAfterWire` 的 N2 在所有 \(2^{I}\) 個輸入上 PO 完全相同。
2. **C432、C3540**：每個候選做 256 組 64-bit 隨機模擬，兩邊 PO 相同；live AND 數必須相等（折疊規則一致的結構檢查）。
3. **dirty 追蹤**：同樣的模擬下，所有 `poDirty == 0` 的 PO 與 golden 相同。
4. **注入錯誤**：隨機翻轉 N2 某個 live AND 的一個輸入極性，交給 `AbcCec`，必須是 Rejected 且反例重播成功。

**通過條件**：
- C432、C499、C3540、C7552 在 `fast`＋e2e 下 Accepted 數與基線相同。
- b15 `rar_bench wire 200` 的 build 加 write ≤ 15 ms/候選。

## 7. Phase 3：寫檔路徑

- `DenseAig::writeBinaryAiger(std::ostream&)`：直接輸出 header、PO literal、delta 編碼，以及 `i<k>`/`o<k>` 符號；用預先 `reserve` 的 `std::string` buffer，一次 `write`。
- `CecJob` 改成攜帶已序列化的 bytes 與重播用的 callback：

```cpp
struct CecJob {
    std::string id;
    std::shared_ptr<const std::string> leftAig;   // 共用：golden 或 golden cone
    std::shared_ptr<const std::string> rightAig;
    std::function<bool(const std::vector<bool>& pi)> replayDiffers;  // 在完整 N0/N2 上重播
};
```

  舊的 `Circuit` 版 `CecJob` 保留一個 adapter，讓 `verify_equiv`、`rar_apply` 等單次命令沿用。
- 非 cone 模式下，golden 的 bytes 每個 worker 只寫一次檔，之後各 batch 都指向同一個檔，不再每個 batch 重寫。
- 反例重播：`replayDiffers` 用 `GoldenContext` 與 `DenseAig` 的 bit 模擬，計算完整 golden 與完整 N2 的 PO。即使 job 只送了 cone，重播仍在完整電路上做。

**通過條件**：b15 `rar_bench` 的 write ≤ 3 ms/候選；所有 CTest 通過。

## 8. Phase 4：只送受影響的輸出錐

### 8.1 做法

1. 由 `CandidateNetlist::poDirty` 取得受影響的 PO 集合 \(P\)，保持原本 PO 順序。
2. 從 golden 和 N2 各抽出 \(P\) 的 fanin cone，寫成兩個 AIGER。**兩邊都保留全部 I 個 PI**，即使沒用到也不刪，這樣 ABC 依位置對 PI 時不會錯位，反例 `i<k>` 也能直接對應原電路。
3. 兩個檔案的 PO 都只放 \(P\)，順序相同（`&cec` 依位置比對 PO）。
4. 若 \(P\) 為空，代表 N2 在結構上與 golden 的所有 PO 相同。仍然送一個 job 給 ABC，內容是完整電路的 e2e；這種情況很少，送完整電路可以維持「每個 Accepted 都來自 ABC」。

### 8.2 安全網

- `set cec_cone on|off`（預設 off，通過驗收後才改成 on）。
- `set cec_audit N`：每 N 個候選，同時送一個完整電路的 e2e job，結論必須與 cone job 相同；不同時整個 run 標成 `cec_cone_mismatch` 並停止。正式 benchmark 用 `N = 100`。
- Rejected 一律再用完整電路 e2e 確認一次，兩者都 Rejected 且重播成功才算 Rejected。

### 8.3 預期效益（實測 b15 隨機 400 個節點的 fanout）

| | 中位數 | p90 | 平均 |
|---|---|---|---|
| 受影響的 PO 數（共 519） | 4 | 291 | |
| 受影響錐的 AND 數（共 8,877） | 2,760 | 7,982 | 3,427（39%） |

ABC 時間大致與送進去的 AND 數成正比，加上 fraig 掃描，預期 cone 模式約省 2–3 倍。若實測不到 1.5 倍，就維持 off。

**通過條件**：
- C432、C499、C3540、C7552 開 cone 加 audit 100 時，Accepted 數與基線相同，`cec_cone_mismatch = 0`。
- 注入錯誤測試在 cone 模式下 100% Rejected。

## 9. Phase 5：大電路跑完並保存

- 一律用 `--shard-wires`（b14、b15 用 2,000；b17 用 4,000），每片各自有 `--case-timeout`。已完成的片靠 checkpoint 跳過（沿用現有的 cached_shards 機制）。
- `rar_batch` 每完成一個 ABC batch 就印 `RAR_PROGRESS done=<n> accepted=<a> elapsed=<s>`，腳本即時寫進 `progress.tsv`。手動中斷時也能看到目前做到哪裡。
- 建議命令（b15）：

```powershell
python satrar-v3\scripts\rar_batch.py --case b15 --modes wire --scope paper `
  --cec-stages e2e --cec-builder fast --cec-cone on --cec-audit 100 `
  --cec-jobs 8 --cec-batch 64 --shard-wires 2000 --case-timeout 1800 `
  --out satrar-v3\results\cec_fast_b15
```

## 10. 預估（依第 0 節實測推算，需由 P0 的 `rar_bench` 確認）

| 電路 | 候選數 | 現在（兩段、reference） | P1（e2e） | P2＋P3 | P4（cone） |
|---|---|---|---|---|---|
| b15 | 37,623 | ≈ 12,800 CPU s（> 60 min 未完成） | ≈ 9,500 CPU s | ≈ 750 CPU s（8 jobs 約 2 min） | ≈ 400 CPU s |
| b14 | 135,384 | ≈ 46,000 CPU s | ≈ 34,000 | ≈ 2,700（8 jobs 約 6 min） | ≈ 1,500 |
| b17 | 123,243 | 約 3.5 倍大的電路，> 100,000 CPU s | | ≈ 9,000（8 jobs 約 20 min） | ≈ 5,000 |

以上都還要加上 discovery 時間：b14 134 s、b15 142 s、b17 562 s。

## 11. Phase 6（選做）：ABC 本身變成瓶頸時

只有在 P4 之後 `cec_abc_seconds` 佔總時間 > 70% 才做：

- **多候選合併成一個 miter**：把 K 個候選的 cone 當成同一個 AIG 的不同 PO 群組，對上 golden cone 複製 K 次（`&cec` 依位置比對），讓 ABC 在一次 fraig 中共用 golden 的結構。失敗時必須知道是哪個 PO 群組，所以要先在 Phase 0 的 probe 環境確認 ABC 能報告失敗的 PO 編號（例如 `&cec -v` 或 `&miter` 加 `&iso`、`&poxsim` 的輸出），確認不了就不做。
- **把 libabc 嵌進 process**：連結 ABC library，透過 `Abc_FrameGetGlobalFrame`、`Cmd_CommandExecute` 在記憶體中傳 Gia，省掉檔案與 process 啟動。授權與建置成本較高，最後才考慮。

## 12. 總驗收

| 項目 | 標準 |
|---|---|
| 正確性 | C432 1112、C499 698、C3540 4804、C7552 15803 全部 Accepted，0 rejected；fig1 八模式全部 Accepted；注入錯誤 100% Rejected 且重播成功；`cec_cone_mismatch = 0`。 |
| fast＝reference | 第 6.7 節測試全部通過。 |
| 速度 | C7552 從 208 s 降到 ≤ 40 s（4 jobs）；b15 ≤ 10 min；b14 ≤ 20 min；b17 ≤ 90 min（8 jobs，含 discovery）。 |
| 完整性 | b14、b15、b17 的 `verify_status = complete`，每個 shard 都有 summary。 |
