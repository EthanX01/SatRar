
# SatRAR verification status

State after implementing `SatRAR_Implementation_Spec.md` R1–R6 and the first part of R7. R8 (conditional) was not started. The large-circuit step was still running when this was written (see "Not finished").

## Evidence

| Scope | Verified outcome | Evidence |
|---|---|---|
| Build and CTest | Visual Studio 2022 x64, Release and Debug, 22/22 passing in each (Release rerun after the last code change; Debug run one change earlier) | `ctest -C Release/Debug` |
| R1 two-stage CEC | Stage-2 failure (throw, timeout, unknown, backend error, SAT without/with corrupt model) keeps the stage-1 `Equivalent` and yields `Unverified` with `failedStage=removal`; construction failure is attributed to the addition stage; a real stage-2 difference is `Rejected`; unavailable backend is never a rejection. Export: temp file, reload, round-trip CEC, atomic rename; every injected failure leaves the old destination and no temp file | `tests/test_verification_stages.cpp` |
| R2 fault / MA / postdominators | 150 seeded random AIGs, 8670 faults (pin and dominator-output, both stuck values): every reported MA literal holds on every detecting vector (13,633 literals), a detectable fault is never reported redundant, `Det(F) ∧ ¬ma` is UNSAT by the exact miter, exact miter SAT/UNSAT agrees with exhaustive simulation, `outputPostdominators` equals a block-and-reach oracle (904 dominators, 936 unobservable gates) | `tests/test_oracle_differential.cpp` |
| R2 Figure 1/6 anchors | Detecting vectors 3 / 3 / 7 (target sa1, g7 sa0, g9 sa0) and 0 for g9 sa0 with g5=0 by truth table; target MA of Example 4; f=1,g4=0 prefix kept with levels and reasons across a switch to g7/g9; g7 trial d=0 consequences and rollback; g9 trial g5=0 conflict | `tests/test_paper_anchors.cpp`, `tests/test_rar_discover.cpp` |
| R2 propagator | Closure equals a freshly built propagator after prefix sharing, nested trials, conflicts and learned clauses (120 seeds); rollback restores the exact trail; reasons are real clauses. This test found a real bug (a late-firing learned clause was lost on backtracking), fixed in `CircuitPropagator::cancelToContextLevel` | `tests/test_propagator_differential.cpp` |
| R2 redundancy events | 34,855 `redundant_target` / `redundant_dominator` events over the random circuits, each confirmed by exhaustive simulation | `tests/test_oracle_differential.cpp` |
| R3 learning and provenance | Learned clauses are installed right after the trial is undone; 34 clauses checked as circuit entailments (CNF ∧ ¬clause UNSAT); `gate_nolearn` learns nothing and still emits conflict candidates; labels follow the number of supporting decisions (fixtures for Corollary 2.1 and 2.2 independent of Figure 1) | `tests/test_provenance_theorems.cpp`, `tests/fixtures/provenance/` |
| R4 Theorem 3 | Positive AND–OR fixture (≥2 OR operands); truth-table separation (`S=1` on target-detecting, `S=0` on dominator-detecting vectors) for every candidate; tampered candidates with an underived operand fail the oracle; one shared budget over trials, splits, probes and deletion retries, observed stopping at `trial`, `sub_probe` and `sub_shrink`, partial results still sound | same test |
| All modes, random AIGs | 7,018 candidates (7 mode variants) all pass independent exhaustive addition and removal equivalence | `tests/test_oracle_differential.cpp` |
| Figure 1 batch | Eight modes, 623 candidate records, all `verified_equivalent` by CaDiCaL (addition and removal). A 4-pin sharded run is identical to the monolithic one except timing; a resume reuses all 5 shards per mode | `results/spec_fig1`, `results/spec_fig1_sharded`, `scripts/compare_runs.py` |
| R5 bookkeeping | Separate search and verify status, per-mode coverage in tables, shards continue after a failure, resume recomputes only missing ranges, forged or corrupt checkpoints are quarantined | `scripts/test_rar_batch.py` (7 tests) |
| R6 inputs | 12 of 12 pinned BENCH files present; b19 fetched, size 14,569,044 and Git blob `150930c1…` verified before writing. Converter tests against an independent BENCH evaluator (all gates and arities, reordered ports, DFF cut, 8 hard-error cases) | `scripts/test_bench_to_aag.py` (6 tests), `results/spec_b19` |
| Four-pin smoke, all inputs | 96 circuit-mode cells (12 circuits × 8 modes) ran at the default budget of 20,000 trials: 90 complete, 6 `search_limited` (reported as such). These are smoke rows (`coverage=smoke`), not full discovery | `results/spec_smoke` |
| Full discovery, small | C432, C499, C6288, b01, b13 in 7 modes (35 cells, budget 1,000,000 trials): all `ok`, `coverage=full`, no limit hit | `results/spec_full_small` |
| Full discovery, C7552 l2 | 22 shards of 250 pins, `coverage=full`, 5264 pins, 108.9 s discovery | `results/spec_full_c7552_l2` |
| Full discovery, medium | C3540 and C7552 in all 7 modes (14 cells, 250-pin shards, budget 1,000,000 trials): all `ok`, `coverage=full`, no limit hit. Discovery seconds, e.g. C3540: wire 5.1, l2 60.3, gate 22.8, gate_nofilter 529.7; C7552: wire 20.0, l2 241.3, gate 42.6, gate_nofilter 234.1 | `results/spec_full_medium` |


At the default budget of 20,000 trials C432 `gate_nofilter` hits the limit in 9 stuck-at cases, all at the last dominator; with 1,000,000 trials it completes. The earlier full runs used 1,000,000 trials, so this is not a regression.

## Changed candidate counts

The Figure 1 count stays 623, but the candidate sets of the learning modes (`gate`, `gate_nofilter`, `sub`) differ from the earlier run because learned clauses now take effect immediately and labels follow the supporting-decision count. `scripts/candidate_diff.py` lists the keys. All candidates of the new run pass both CEC stages; the removed candidates were not individually re-derived. `wire`, `l0`–`l2` and `gate_nolearn` are unchanged.

## Not finished

* `spec_full_large` (b14, b15, b17; wire, l0, l1; 1000-pin shards) was started and is slow (about 20 s per shard, roughly 300 shards); it was still running when this file was written, so it has no result here. It resumes from its checkpoints with `.\satrar-cpp\scripts\run_spec_experiments.ps1 -Only spec_full_large`. b18/b19 full discovery was not attempted.
* No Table I/II or Figure 7 comparison with the paper's numbers has been produced. The paper's `#wire` is now stored in the manifest (`paper_wire_count`); for example C3540 2939 equals the BENCH pin count while the AIG has 2540 pins, so equal single counts do not show equal topology. No source-level (non-AIG) topology profile exists.
* R7 profiling counters exist (`prof_*` columns: setup, projection, trials, canonicalization; CEC is `verify_seconds`) but no bottleneck analysis or optimization was done. Peak memory in `run_report.json` covers the Python runner only.
* Checkpoints are not reused across different timeout or trial caps (a different cap is a different key).
* R8 (extended source coverage, original-topology profile, arrival-time ordering) was not started.
* `docs/Cursor_Composer_SatRAR_Plan_v2.md` and the 2026-11-24 audit still describe the earlier state.
