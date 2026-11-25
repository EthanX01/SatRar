#pragma once

#include "satrar/aig/Literal.hpp"
#include "satrar/sat/CnfFormula.hpp"
#include "satrar/sat/DimacsLiteral.hpp"

namespace satrar::sat {

/// Internal AIG keys for fixed constants in CNF (not AAGER wire ids).
constexpr std::uint32_t kCnfConstFalseKey = 0xE0000000u;
constexpr std::uint32_t kCnfConstTrueKey = 0xE0000001u;

struct CnfConstVars {
    std::uint32_t falseVar = 0;
    std::uint32_t trueVar = 0;
};

/// Reserve SAT vars and unit-clause fix falseVar=0, trueVar=1.
inline CnfConstVars bindAigConstants(CnfFormula& formula) {
    CnfConstVars vars;
    vars.falseVar = formula.ensureAigVar(kCnfConstFalseKey);
    vars.trueVar = formula.ensureAigVar(kCnfConstTrueKey);
    formula.addClause({dimacsFromVar(vars.falseVar, false)});
    formula.addClause({dimacsFromVar(vars.trueVar, true)});
    return vars;
}

/// If keys already present, only return SAT indices (no duplicate unit clauses).
inline CnfConstVars ensureAigConstants(CnfFormula& formula) {
    const bool had = formula.aigToSat().count(kCnfConstFalseKey) != 0;
    CnfConstVars vars;
    vars.falseVar = formula.ensureAigVar(kCnfConstFalseKey);
    vars.trueVar = formula.ensureAigVar(kCnfConstTrueKey);
    if (!had) {
        formula.addClause({dimacsFromVar(vars.falseVar, false)});
        formula.addClause({dimacsFromVar(vars.trueVar, true)});
    }
    return vars;
}

/// DIMACS literal true in model iff AIGER edge evaluates to 1 (respects inversion on the edge).
inline DimacsLiteral aigLiteralToDimacs(const aig::Literal lit, const CnfConstVars& constants,
                                        const CnfFormula& formula) {
    if (lit.isConstant()) {
        const DimacsLiteral base = lit.valueIfConstant() ? dimacsFromVar(constants.trueVar, true)
                                                         : dimacsFromVar(constants.falseVar, true);
        return base;
    }
    const std::uint32_t satVar = formula.satVarOfAig(lit.variable());
    return dimacsFromVar(satVar, !lit.inverted());
}

inline DimacsLiteral aigLiteralToDimacs(const aig::Literal lit, const CnfConstVars& constants,
                                        const CnfFormula& formula, std::uint32_t aigKey) {
    if (lit.isConstant()) {
        return aigLiteralToDimacs(lit, constants, formula);
    }
    const std::uint32_t satVar = formula.satVarOfAig(aigKey);
    return dimacsFromVar(satVar, !lit.inverted());
}

}  // namespace satrar::sat
