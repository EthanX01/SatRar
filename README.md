# SatRAR

Two C++ implementations of Wu, Lin, Huang, and Huang, ASP-DAC 2009.

| Directory | What it is |
|---|---|
| `satrar-cpp` | Clause-based SatRAR. AIG circuits are encoded to CNF and propagated with watched clauses. |
| `satrar-circuit-v2` | Gate-native SatRAR. BCP, reasons, and 1-UIP run directly on AIG AND gates. Equivalence checking still uses CaDiCaL. |

`satrar-circuit-v2` reads CaDiCaL from `satrar-cpp/third_party/cadical`. Build both trees from this directory. The paper PDF and the implementation spec sit next to the code.

Generated `build/` and `build-msvc/` directories are not part of the repository.
