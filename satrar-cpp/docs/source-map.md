
# Source map

| Code | Role | Evidence |
|---|---|---|
| `GraphAnalysis`, `FaultContext` | Target branch, downstream postdominators, excitation and observation | Paper §II, Figure 1 |
| `CircuitPropagator`, `MandatoryAssignments` | Native AND implications, trail, reasons, reusable fault prefix, immediate installation of learned clauses (`learnNow`) with replay of learned units after backtracking | Paper §IV, Figure 5/6 |
| `SatRarEngine` | 2-Way levels 0–2, SRAR_Wire, SRAR_Gate and AND–OR alternatives; shared trial budget; redundancy events; optional trace | Paper §§II–IV, Theorems 1–3 |
| `Rewriter`, `RarVerifier` | Distinct addition/removal, per-stage evidence (`failedStage`), miter proof | Paper §§I–II and implementation verification |
| `ExportAag` | Write next to the destination, re-read from disk, round-trip CEC, atomic rename | Implementation safety |
| `MiterBuilder`, `CadicalBackend` | Positional PI sharing, separate internal variables and replayable counterexamples | Exact verification for the AIG implementation |
| `scripts/rar_batch.py` | Mode comparison, coverage-aware shards, checkpoints, source/converted counts | Paper §V, Tables I and II |
| `scripts/bench_to_aag.py` | BENCH to AIG with topology metadata (`original_wires`, gate arity, pin mapping classes) | Paper §V input slots |

## Paper statements versus the implementation

| Anchor | Statement in the paper | What the code does |
|---|---|---|
| Theorem 2 (p.193) eq. (2) | The printed conclusion names `MA(w_t)`. | By the premises and De Morgan the conclusion is `MA(g_d) ⇒ ¬g_n`. The code proves the dominator-side implication (`Φ_d ∧ trial ⇒ ¬operand`) and the target-side value (`operand ∈ Φ_t`); tests check both with an independent truth table. |
| Theorem 3 (p.194) eq. (3) | The proof expands only to `b_(n-1)`. | The theorem needs the negation of **every** `b_i` on the dominator side. Each OR operand is a literal taken from the dominator trial trail, so every `¬b_i` has a recorded derivation. `test_provenance_theorems` rejects a tampered candidate whose extra operand is not derived (`Φ_d ∧ A ∧ b_n` satisfiable) and checks `S = 1` on all target-detecting and `S = 0` on all dominator-detecting vectors. |
| Corollary 2.1 / 2.2 | Two root decisions for conflict / reverse implication. | Labels follow the number of **supporting** trial decisions (not trial depth): conflict 1 = Theorem-1, 2 = Corollary-2.1, more = `Generalized-2.1`; reverse implication 1 root + operand = Theorem-2, more = Corollary-2.2. Zero roots is a contradictory base and becomes a `redundant_dominator` event, never a candidate. |
| Figure 6 level 0 | The fault propagation layer is drawn as level 0. | In the implementation each context assumption has its own undoable level above the solver's real root. Evidence (reasons, levels) is reported with real levels. |
| Figure 5 lines 4–5, 9–10 | Redundant target / dominator ends the attempt. | `SatRarStatistics::redundancy` records `redundant_target` / `redundant_dominator`; batch mode keeps searching other dominators and counts the events (never as an alternative). |
| §II.A | Pin fault excitation. | The raw literal driving the pin is forced to `1-s`; the faulty copy overrides only that pin. Raw AND output faults are sa0; the logic gate g7 of Figure 1 is `AND` (var 13) while g5, g6, g8 are complemented ANDs, so paper outputs sa1 map to raw sa0. |
