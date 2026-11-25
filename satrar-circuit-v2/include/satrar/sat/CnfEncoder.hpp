#pragma once

#include "satrar/aig/Circuit.hpp"
#include "satrar/sat/CnfFormula.hpp"
#include "satrar/sat/DimacsLiteral.hpp"

#include <optional>

namespace satrar::sat {

class CnfEncoder {
public:
    static CnfFormula encodeCircuit(const aig::Circuit& circuit);

    /// Positive-phase DIMACS literal for an AIG variable after \p encodeCircuit (if encoded).
    static std::optional<DimacsLiteral> positiveLit(const CnfFormula& formula, std::uint32_t aigVar);

    /// Map an AIGER fanin literal to DIMACS (same convention as Tseitin encoding).
    static std::optional<DimacsLiteral> dimacsLitForAig(const CnfFormula& formula, aig::Literal lit);
};

}  // namespace satrar::sat