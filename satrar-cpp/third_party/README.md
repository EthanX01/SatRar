# Third-party dependencies

## MiniSat (in-process propagation backend)

- **Upstream (read-only):** `../../minisat-solver/` (do not edit).
- **Vendored patch file:** `vendored_minisat/Solver.cc` — copy of `minisat/core/Solver.cc` with MSVC-safe `printf` spacing for `PRIu64` in `printStats()` only.
- **Stub header:** `compat/zlib.h` — satisfies `ParseUtils.h` include; gzip DIMACS is not used in-process.

Core sources compiled into CMake target `satrar_sat`: vendored `Solver.cc`, upstream `utils/Options.cc`, `utils/System.cc`, plus `MinisatPropagationEngine.cpp`. Call `Solver::newVar()` for each DIMACS variable before adding clauses.

## CaDiCaL (Phase 11 — verification CLI)

- **Source:** vendored clone `third_party/cadical` (tag **rel-2.1.0**).
- **Build (Windows):** `.\scripts\build_cadical.ps1` → `third_party/cadical/build/cadical.exe` (MinGW + `-lpsapi`).
- **Configure test/backend:** `SATRAR_CADICAL_EXE` or CMake cache `SATRAR_CADICAL_EXECUTABLE`.
- **Exit codes:** 10 = SAT, 20 = UNSAT (standard CaDiCaL CLI).

## AIGER fixtures

Official examples under `../tests/fixtures/` (`and.aag`, `halfadder.aag`).