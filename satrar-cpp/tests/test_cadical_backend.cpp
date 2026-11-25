#include "satrar/sat/CadicalBackend.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d (%s)\n", __FILE__, __LINE__, #cond);            \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

int main() {
    satrar::sat::CadicalBackend backend;
    if (backend.executable().empty()) {
        std::fprintf(stderr,
                    "test_cadical_backend: SKIP (cadical not found — run scripts\\build_cadical.ps1)\n");
        return 77;
    }
    CHECK(backend.available());
    std::fprintf(stderr, "[satrar][INFO] %s\n", backend.describe().c_str());

    const auto fx =
        std::filesystem::path(__FILE__).parent_path() / "fixtures";
    const auto unsat = fx / "unsat_unit.cnf";
    const auto sat = fx / "sat_unit.cnf";

    const auto ru = backend.solveDimacsFile(unsat, 30u);
    CHECK(ru.verdict == satrar::sat::SatVerdict::Unsat);
    CHECK(ru.exitCode == 20);
    std::fprintf(stderr, "[satrar][OK] UNSAT %s (%.3fs) %s\n", unsat.string().c_str(),
                ru.elapsedSeconds, ru.message.c_str());

    const auto rs = backend.solveDimacsFile(sat, 30u);
    CHECK(rs.verdict == satrar::sat::SatVerdict::Sat);
    CHECK(rs.exitCode == 10);
    CHECK(!rs.modelLits.empty());
    std::fprintf(stderr, "[satrar][OK] SAT %s (%.3fs) model_lits=%zu\n", sat.string().c_str(),
                rs.elapsedSeconds, rs.modelLits.size());

    std::fprintf(stderr, "test_cadical_backend: ok\n");
    return 0;
}
