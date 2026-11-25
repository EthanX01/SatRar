# SatRAR circuit V2

Gate-native SatRAR. The solver propagates directly on AIG AND gates (trail, reasons, guarded clauses, DPLL and 1-UIP) instead of scanning a Tseitin CNF. Equivalence checking still uses a separate CNF encoder and CaDiCaL.

The AIG reader, rewriter, and CEC flow are ported from `../satrar-cpp`. The SAT kernel, fault miter, and search are new. See `docs/V2_STATUS.md`.

```powershell
cmake -S satrar-circuit-v2 -B satrar-circuit-v2/build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build satrar-circuit-v2/build-msvc --config Release
ctest --test-dir satrar-circuit-v2/build-msvc -C Release --output-on-failure
```

CaDiCaL is compiled from `../satrar-cpp/third_party/cadical`. The executable is `build-msvc/Release/satrar_v2.exe`.

```text
read tests/fixtures/paper/fig1.aag
fault_ma pin 13 0 sa1
fault_ma dom 13 sa0 trial -4
rar_verify 13 0 sa1 wire 10
rar_apply 0 results/fig1-rewritten.aag 10
```

Signed integers in the shell are AIG variable numbers: `4` forces variable 4 true, `-4` forces it false.

An interactive terminal shows the `satrar_v2>` prompt. Piped input keeps the existing batch format. Quote AAG paths that contain spaces.

Use `rar_search` to list alternatives or `rar_verify` to list and check them. Both show the target pin and source, the counts of wire, gate, and subcircuit candidates, and each candidate's attachment output and replacement expression. `v15` denotes positive AIG variable 15 and `~v11` denotes the complement of variable 11. For example, `at_output=v15 replacement=(v15 AND ~v11)` means to add an AND wrapper at output 15 with the complemented variable 11 as its other input. The candidate key remains available for batch scans. `fault_ma` reports assignments and `fault_exact` reports detectability.
