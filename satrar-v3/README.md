# satrar-v3

SatRAR with ABC as the only equivalence checker. Search, mandatory assignments, and rewriting come from `satrar-cpp`. CaDiCaL is not used.

ABC is the `yosys-abc.exe` installed for Phase 0:

```text
../satrar-cpp/tools/abc/bin/yosys-abc.exe
```

Override it with `SATRAR_ABC_EXE` or `set abc_exe <path>` in the shell.

## Build

```powershell
cmake -S satrar-v3 -B satrar-v3/build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build satrar-v3/build-msvc --config Release
ctest --test-dir satrar-v3/build-msvc -C Release --output-on-failure
```

## Check a circuit

```text
read path.aag
verify_equiv other.aag 30
rar_wire <andVar> <pin> sa0
rar_batch wire 0 30 paper verify summary 1000000 0
show cec
```

`rar_batch` verifies candidates in batches with `&cec`. `two` checks both rewrite stages. `set cec_stages e2e` checks only the original circuit against the final rewrite.

```powershell
python -u satrar-v3/scripts/rar_batch.py --manifest satrar-cpp/benchmarks/manifests/aspdac2009.json `
  --case C432 --modes wire --max-trials 1000000 --solver-timeout 30 --case-timeout 1800 `
  --summary-only --cec-jobs 4 --out satrar-v3/results/cec_abc_c432
```
