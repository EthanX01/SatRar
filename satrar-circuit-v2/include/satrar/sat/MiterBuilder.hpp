#pragma once

#include "satrar/aig/Circuit.hpp"
#include "satrar/sat/CnfFormula.hpp"
#include "satrar/sat/DimacsLiteral.hpp"

#include <vector>

namespace satrar::sat {

/// Encodings share positional PIs and allocate their own AND variables.
enum class MiterSide { A, B };

/// Build a miter CNF: SAT <=> at least one PO differs between equivalent-layout circuits.
class MiterBuilder {
public:
    struct EncodedCircuit {
        CnfFormula cnf;
        std::vector<DimacsLiteral> primaryOutputLits;
        std::unordered_map<std::uint32_t, DimacsLiteral> signals;
    };

    [[nodiscard]] static EncodedCircuit encode(const aig::Circuit& circuit, MiterSide side,
                                                CnfFormula* appendInto = nullptr);

    /// Assumes same PI count/order and PO count/order. Clauses in \p out imply miterOut is true when POs differ.
    [[nodiscard]] static DimacsLiteral buildXorMiter(const EncodedCircuit& golden,
                                                     const EncodedCircuit& candidate,
                                                     CnfFormula& out);

    [[nodiscard]] static CnfFormula buildNotEquivalentFormula(const aig::Circuit& golden,
                                                                 const aig::Circuit& candidate);

    /// Positional PI sharing: i-th primary input on A/B maps to the same SAT variable.
    [[nodiscard]] static std::uint32_t primaryInputEncodingKey(std::size_t piIndex);
};

}  // namespace satrar::sat
