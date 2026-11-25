# SatRAR V2 status

2026-11-25. Project root: `satrar-circuit-v2/`. Plan: `SatRAR_Circuit_Based_V2_Plan.md`.

## Layout

| Target | Role |
|---|---|
| `satrar_v2_aig` | AAG I/O, circuit, simulation. Ported from `satrar-cpp`. |
| `satrar_v2_solver` | `GateNet` + `SolverSession`. No CNF encoder. |
| `satrar_v2_rar` | Postdominators, native fault miter, SatRAR search, rewriter. |
| `satrar_v2_verify` | Tseitin CNF, CaDiCaL, two-stage CEC, atomic AAG export. |
| `satrar_v2` | CLI. |

`solver` links only `aig`. Verification CNF does not link into the solver tests.

## What each stage does

| Stage | Entry | Acceptance already run |
|---|---|---|
| V0 | `AagParser` / `AagWriter` | `test_aag_roundtrip`: Figure 1 signed edges reload and match on one PI vector. |
| V1 | `SolverSession` | `test_gate_bcp`: 27 partial assignments for a plain AND, plus inverted pins, `AND(x,x)`, `AND(x,~x)`, constants. Fresh session, rollback, and a learned clause agree. |
| V2 | `FaultContext` | `test_fault_ma`: Figure 1 detecting counts 3/3/7. Target MAs, Figure 6 prefix `f=1` then `g4=0`, `d=0` trial and rollback, `g9` plus `g5=0` conflicts. Each reported target MA holds on every detecting vector. |
| V3 | `SatRarEngine`, `RarVerifier` | `test_rar_anchors`: `g5→g9` is absent in L0/L1 and present in L2. Gate example separator raw `{9,22}` on g7. Corollary 2.1, 2.2, and Theorem 3 fixtures match their labels. Eight modes (including `gate_nolearn` / `gate_nofilter`) pass exhaustive two-stage equivalence. `test_cec`: every Figure 1 wire candidate is CEC-accepted and one rewrite reloads from disk. |
| V4 | `SolverSession::solve`, `fault_exact`, `ma_check` | `test_exact`: DPLL and 1-UIP agree with simulation on SAT, UNSAT, a second SAT, a decision limit (`UNKNOWN`), a replayed PI witness, redundant-vs-mandatory, and `g9 sa0` with `g5=0`. Learned clauses on the AND gate are true for every PI vector. |
| V5 | `scripts/compare_v1_v2.ps1` | Writes `results/v5/fig1_modes.tsv` and per-mode canonical keys. Large Table I/II benches are the same CLI with a higher trial cap; they are not part of the unit tests. |

## Commands that passed on Figure 1

From `satrar-circuit-v2` after a Release build:

```text
read tests/fixtures/paper/fig1.aag
fault_ma pin 13 0 sa1
fault_ma dom 13 sa0 trial -4
rar_verify 13 0 sa1 wire 10
rar_apply 0 results/fig1-rewritten.aag 10
sat_solve 15
fault_exact pin 13 0 sa1
ma_check pin 13 0 sa1 12
```

`12` is AIG variable 12 true, which is paper `g6=0`.

## Build

Release and Debug CTest: 6/6 passed (`test_gate_bcp`, `test_fault_ma`, `test_rar_anchors`, `test_exact`, `test_aag_roundtrip`, `test_cec`).

```powershell
cmake -S satrar-circuit-v2 -B satrar-circuit-v2/build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build satrar-circuit-v2/build-msvc --config Release
ctest --test-dir satrar-circuit-v2/build-msvc -C Release --output-on-failure
cmake --build satrar-circuit-v2/build-msvc --config Debug
ctest --test-dir satrar-circuit-v2/build-msvc -C Debug --output-on-failure
```

## Figure 1 discovery (V5)

`scripts/compare_v1_v2.ps1` on the Release binary, target `g6→g7 sa1`. Seconds include process startup.

| mode | candidates | trials | learned | gate visits | seconds |
|---|---:|---:|---:|---:|---:|
| l0 | 1 | 0 | 0 | 65 | 0.038 |
| l1 | 3 | 12 | 0 | 148 | 0.025 |
| l2 | 4 | 28 | 0 | 221 | 0.025 |
| wire | 4 | 9 | 3 | 172 | 0.026 |
| gate | 16 | 13 | 4 | 214 | 0.027 |
| gate_nolearn | 20 | 16 | 0 | 250 | 0.029 |
| gate_nofilter | 24 | 51 | 4 | 529 | 0.038 |
| sub | 20 | 27 | 4 | 214 | 0.030 |

Keys are in `results/v5/fig1_<mode>.keys.txt`. L2 and wire both contain `13,0,1,15,w,23` (`g5→g9`).

## Not claimed

- Candidate counts are not forced to equal the V1 623-key Figure 1 total. Keys are canonical and CEC-checked; a count difference is a key diff, not a silent pass.
- ASPDAC Table I/II on C432–b19 has a manifest (`benchmarks/manifests/aspdac2009.json`) and the same shell, but those runs were not executed here.
- Paper scope (`exclude the direct receiver`) is `SatRarOptions::excludeTargetDirectReceiverGates`. The Figure 1 shell keeps the receiver so the g7 example stays visible.
