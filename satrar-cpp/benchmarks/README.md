# SatRAR benchmark inputs

Pre-packaged **ASCII AIGER (`.aag`)** circuits for manual runs and future batch scripts.

## Layout

| Directory | Contents | Source |
|-----------|----------|--------|
| `inputs/epfl-arithmetic/` | 10 EPFL arithmetic benchmarks | [lsils/benchmarks](https://github.com/lsils/benchmarks) (`.aig` → `.aag`) |
| `inputs/epfl-random_control/` | 10 EPFL random/control benchmarks | same |
| `inputs/aiger-examples/` | Official small teaching AIGs | [arminbiere/aiger `examples/`](https://github.com/arminbiere/aiger) |
| `vendor/` | Shallow git clones (re-download with scripts below) | not required if `inputs/` is present |
| `tools/aigtoaig.exe` | AIGER format converter (optional, local build) | built from `vendor/aiger` |

EPFL **MtM** circuits (multi-million-gate) are **not** included here; they live in the [Zenodo EPFL archive](https://zenodo.org/records/2572934) (~2 GB) as separate downloads.

## Licenses / citation

- EPFL suite: MIT ([lsils/benchmarks](https://github.com/lsils/benchmarks)); see also the [EPFL LSI page](https://www.epfl.ch/labs/lsi/page-102566-en-html/benchmarks/).
- AIGER tools/examples: MIT ([arminbiere/aiger](https://github.com/arminbiere/aiger)).

## Refresh or extend

```powershell
cd satrar-cpp\benchmarks
git clone --depth 1 https://github.com/lsils/benchmarks.git vendor\epfl
git clone --depth 1 https://github.com/arminbiere/aiger.git vendor\aiger

# Build converter (MinGW gcc on this machine):
C:\MinGW\bin\gcc.exe -O2 -o tools\aigtoaig.exe vendor\aiger\aigtoaig.c vendor\aiger\aiger.c

# Convert all EPFL binary AIGs:
$conv = ".\tools\aigtoaig.exe"
Get-ChildItem vendor\epfl\arithmetic\*.aig | ForEach-Object {
  & $conv $_.FullName "inputs\epfl-arithmetic\$($_.BaseName).aag"
}
Get-ChildItem vendor\epfl\random_control\*.aig | ForEach-Object {
  & $conv $_.FullName "inputs\epfl-random_control\$($_.BaseName).aag"
}
Copy-Item vendor\aiger\examples\*.aag inputs\aiger-examples\
```

## One-shot verify (build + CTest + all benchmarks)

```powershell
.\satrar-cpp\scripts\run_all.ps1
```

Prints `[satrar][PASS|SKIP|FAIL]` per `.aag` file on the console.

## Run with `satrar` (from repo root)

```powershell
$exe = .\build-msvc\Release\satrar.exe
@'
read benchmarks\inputs\epfl-random_control\ctrl.aag
stats
write_cnf results\ctrl.cnf
quit
'@ | & $exe --shell
```

**Parser limits:** ASCII `.aag` only (not binary `.aig`); **no latches** — sequential AIGER examples under `aiger-examples/` (e.g. `cnt1*.aag`) will be rejected until latch support exists.

## Manifest

See `manifests/epfl_combinational.json` for the list of circuits and paths.