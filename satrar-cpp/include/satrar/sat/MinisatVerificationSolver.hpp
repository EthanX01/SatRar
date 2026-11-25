#pragma once

#include "satrar/sat/CnfFormula.hpp"
#include "satrar/sat/SatBackend.hpp"

namespace satrar::sat {

/// In-process full SAT on a `CnfFormula` (Phase 16A regression; not propagation).
class MinisatVerificationSolver {
public:
    [[nodiscard]] static SatSolveResult solve(const CnfFormula& formula);
    [[nodiscard]] static bool available() noexcept;
};

}  // namespace satrar::sat