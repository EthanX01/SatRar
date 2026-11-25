#include "satrar/sat/MinisatVerificationSolver.hpp"

#include "minisat/core/Solver.h"
#include "minisat/mtl/Vec.h"

namespace satrar::sat {

namespace {

Minisat::Lit toMsLit(DimacsLiteral lit) {
    const int v = lit > 0 ? lit : -lit;
    return Minisat::mkLit(static_cast<Minisat::Var>(v - 1), lit < 0);
}

}  // namespace

bool MinisatVerificationSolver::available() noexcept {
    return true;
}

SatSolveResult MinisatVerificationSolver::solve(const CnfFormula& formula) {
    SatSolveResult result;
    Minisat::Solver solver;
    const std::uint32_t n = formula.numVariables();
    while (static_cast<std::uint32_t>(solver.nVars()) < n) {
        solver.newVar();
    }
    for (const auto& clause : formula.clauses()) {
        Minisat::vec<Minisat::Lit> ps;
        ps.growTo(static_cast<int>(clause.size()));
        for (int i = 0; i < ps.size(); ++i) {
            ps[i] = toMsLit(clause[static_cast<std::size_t>(i)]);
        }
        if (!solver.addClause(ps)) {
            result.verdict = SatVerdict::Unsat;
            result.message = "empty clause in CNF";
            return result;
        }
    }

    const bool sat = solver.solve();
    if (!sat) {
        result.verdict = SatVerdict::Unsat;
        result.message = "UNSAT";
        return result;
    }

    result.verdict = SatVerdict::Sat;
    result.message = "SAT";
    result.modelLits.clear();
    result.modelLits.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        const Minisat::lbool v = solver.modelValue(static_cast<Minisat::Var>(i));
        if (v == Minisat::lbool((uint8_t)0)) {
            result.modelLits.push_back(static_cast<std::int32_t>(i + 1));
        } else if (v == Minisat::lbool((uint8_t)1)) {
            result.modelLits.push_back(-static_cast<std::int32_t>(i + 1));
        }
    }
    return result;
}

}  // namespace satrar::sat
