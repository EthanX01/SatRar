
# satrar-cpp

C++17 implementation of the SatRAR algorithms from Wu, Lin, Huang and Huang, ASP-DAC 2009. Inputs are combinational ASCII AIGER circuits. The batch runner also converts gate-level BENCH input with an explicit DFF cut for sequential designs.

## Build and test

Run from the EdaLab directory with Visual Studio Build Tools 2022.

```powershell
$cm = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cm -S satrar-cpp -B satrar-cpp/build-msvc -G 'Visual Studio 17 2022' -A x64
& $cm --build satrar-cpp/build-msvc --config Release
& (Join-Path (Split-Path $cm) 'ctest.exe') --test-dir satrar-cpp/build-msvc -C Release --output-on-failure
```

The build produces both `satrar.exe` and `satrar_cadical.exe` in the same configuration directory. CaDiCaL 2.1.0 builds from the included source. An explicit `SATRAR_CADICAL_EXE` environment variable or CMake `SATRAR_CADICAL_EXECUTABLE` can override it. `SATRAR_BUILD_CADICAL=OFF` leaves external verification optional. MiniSat is used in the exact fault oracle.

## Figure 1

Open `.\satrar-cpp\build-msvc\Release\satrar.exe --shell` from EdaLab and enter these commands.

```text
read satrar-cpp/tests/fixtures/paper/fig1.aag
fault_ma pin 13 0 sa1 exact
fault_ma dom 13 sa0 trial -4 exact
rar_verify 13 0 sa1 wire 10
rar_verify 13 0 sa1 gate 10
```

The `g5 → g9` candidate has `attach=15` and `source_raw=23`. `rar_apply <id> "path.aag" 10` exports a selected candidate after verifying addition, removal and the serialized round-trip. Candidate IDs belong to the last search on the loaded circuit.

Use `rar_l0`, `rar_l1`, `rar_l2`, `rar_wire`, `rar_gate`, `rar_gate_nolearn`, `rar_gate_nofilter` and `rar_sub` for the individual discovery modes. `trace on` prints a stable `TRACE` line for every scope, trial, conflict, learned clause and emitted candidate of the next searches. A `rar_verify` line reports `acceptance=accepted|rejected|indeterminate` and, for anything but `accepted`, the first stage (`addition` or `removal`) that did not give a trusted equivalence; the evidence of the stage before it is kept.

`rar_apply` writes the verified rewrite to a temporary file next to the destination, reloads it from disk, proves the reload equivalent to the original and only then replaces the destination, so a failed export never leaves a truncated or half-checked file.

## Experiments

Run `python satrar-cpp/scripts/rar_batch.py --scope all` for the Figure 1 regression in all modes. Pass a manifest and a wire limit to run a benchmark subset. The runner records per-target and per-candidate TSV, coverage-aware table aggregates, input hashes and resume checkpoints. `scripts\run_spec_experiments.ps1` reruns the whole matrix described in [the protocol](docs/experiment-protocol.md); `scripts/compare_runs.py` and `scripts/candidate_diff.py` compare two runs. Python bookkeeping tests: `python -m unittest scripts/test_rar_batch.py scripts/test_bench_to_aag.py`.

- [Algorithm notes](docs/algorithm-notes.md)
- [Source map and paper differences](docs/source-map.md)
- [Experiment protocol](docs/experiment-protocol.md)
- [Verification status](results/PHASE_STATUS.md)
