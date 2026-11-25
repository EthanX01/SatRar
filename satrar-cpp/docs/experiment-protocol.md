
# SatRAR experiment protocol

Use the Release build. `scripts/rar_batch.py` writes one checkpoint per shard, keyed by the executable and solver hashes, source and converted hashes, converter version, topology profile, pin order, mode, scope, search budget and timeouts. Only fully finished shards (search complete and, when verifying, no unverified candidate) are checkpointed; unfinished work is retried. Use distinct output directories for independent runs.

```powershell
# Everything below in one go (each step has its own results\spec_* directory):
.\satrar-cpp\scripts\run_spec_experiments.ps1 -Fresh

python satrar-cpp/scripts/rar_batch.py --scope all --out satrar-cpp/results/spec_fig1
python satrar-cpp/scripts/rar_batch.py --scope all --shard-wires 4 --out satrar-cpp/results/spec_fig1_sharded
python satrar-cpp/scripts/compare_runs.py satrar-cpp/results/spec_fig1 satrar-cpp/results/spec_fig1_sharded
python satrar-cpp/scripts/rar_batch.py --manifest satrar-cpp/benchmarks/manifests/aspdac2009.json --wire-limit 4 --discover-only --out satrar-cpp/results/spec_smoke
python satrar-cpp/scripts/rar_batch.py --manifest satrar-cpp/benchmarks/manifests/aspdac2009.json --case b19 --modes wire --fetch-missing --wire-limit 4 --discover-only --out satrar-cpp/results/spec_b19
python satrar-cpp/scripts/candidate_diff.py OLD_RUN NEW_RUN --mode gate   # candidate key diff between two runs
```

## Outputs

| Output | Meaning |
|---|---|
| `summary.tsv` | One row per circuit and mode: `search_status`, `verify_status`, `coverage`, `reasons`, counts, separate discovery / verification / wall seconds |
| `targets.tsv` | Receiving pin and stuck value: alternatives, limit step and scope, redundant dominators, trials, Theorem-3 probes |
| `candidates.tsv` | Target, attachment, signed AIG expression, both CEC outcomes, failing stage and time |
| `table_I.tsv` / `table_II.tsv` | `wire/l0/l1/l2` and `wire/gate/gate_nolearn/gate_nofilter`; every mode keeps its own status, coverage, `searched_wires`, `complete_wires` and counts |
| `settings.json`, `run_report.json` | Fingerprint of binaries and options; wall time and peak memory of the runner process (the `satrar` child is not included) |
| `converted/*.map.json` | Source hashes and Git blob, explicit latch cuts, port order, signal map, gate arity histogram, pin-mapping classes, `original_wires` and `aig_wires` |

## Status vocabulary

Search and verification are separate fields; `status` is only a compact summary and `reasons` lists every cause (`search:partial@8+4;verify:incomplete@12+4`).

| Field | Values |
|---|---|
| `search_status` | `complete`, `partial` (a trial budget stopped at least one stuck-at case), `timeout`, `process_error`, `input_error` |
| `verify_status` | `complete` (no unverified candidate), `incomplete`, `unchecked` (`--discover-only`), `not_run` |
| `coverage` | `full` (contiguous shards over every pin, both stuck values finished, no limit hit), `partial`, `smoke` (`--wire-limit` smaller than the circuit; never a full run), `none` |
| `status` | `ok`, `search_limited`, `verification_incomplete`, `timeout`, `process_error`, `input_error` |

Input failures carry a reason: `missing_input`, `fetch_failed`, `blob_mismatch` (the file differs from its pinned Git blob), `conversion_error`.

A pin is **complete** only if neither of its two stuck-at cases hit the budget; a limit in one case marks the pin as entered but incomplete. Shards partition pins, so both stuck values of a pin always run in the same shard. A failed shard never stops the others, a later failure never replaces an earlier reason, and a resume recomputes only the missing or rejected ranges. Checkpoints are validated (schema, ranges, non-negative counts, coverage, `searched_wires`, status/limit agreement, duplicate or missing rows); a rejected file is renamed `*.corrupt-<time>` and recomputed.

## Counting

* `#wire` is the AIG receiving-pin count (`aig_wires`). `original_wires` is the BENCH fanin-pin count plus one edge per cut DFF data input (PO ports excluded). `paper_wire_count` is the paper's printed `#wire` at its precision; equality of one count does not prove an identical topology (see below).
* `#tar` counts distinct receiving pins with at least one candidate (both stuck values count once). `#alt` counts candidates keyed by pin, stuck value, attachment and signed expression. `#T` is discovery seconds; verification (`verify_seconds`) and runner `wall_seconds` (process start, parse, shard overhead) are separate columns. CEC time is not part of `#T`.
* Search budget `--max-trials` is shared by outer trials, recursive-learning branches, Theorem-3 probes and operand-deletion retries. Reaching it marks the case `partial`.
* Scope `paper` excludes candidates attached directly to the target's receiving gate; scope `all` keeps them (needed for the g7 gate example). The mode `sub` and the scope `all` are reported separately from Tables I/II.

## Topology profile and comparability

The recorded profile is `aig`: BENCH gates are decomposed into two-input ANDs. Pin classes recorded per input: two-input AND/NAND/OR/NOR map one to one up to pin order (the AIG keeps the larger literal on pin 0), multi-input gates and XOR/XNOR decompose one to many, NOT/BUF pins vanish. With these inputs the converted wire count differs from the paper's for most circuits (for example C3540: 2939 BENCH pins = the paper's `#wire`, 2540 AIG pins). A source-level profile that keeps the original gates is **not** implemented; counts and times are results of this AIG implementation on the recorded inputs (mirror variants of ISCAS85 / ITC99 BENCH files, see the manifest) and on Windows x64, not of the paper's Linux/Xeon/QuteSAT setup. Reference values from the paper stay in the paper's own columns; paper timing and ratios are not reproduced by these runs.

Figure 7 style timing must come from fresh runs of one Release build in one session: do not mix cached shards (`cached_shards` > 0) into timing comparisons.

## Inputs

The ASP-DAC manifest pins every source by URL and Git blob ID (`SHA1("blob " + size + "\0" + bytes)`). `--fetch-missing` downloads a missing file, checks the blob ID before writing, and fails with `fetch_failed` / `blob_mismatch` otherwise. b19 (14,569,044 bytes, blob `150930c1502857fe981527df3c5da6aff2e72a3a`) is checked in `benchmarks/inputs/itc99`.
