#pragma once

#include "satrar/aig/Circuit.hpp"
#include "satrar/rar/Alternative.hpp"

namespace satrar::rar {

/// Phase-12 minimal rewriter: apply one wire bypass on a circuit clone.
class CircuitRewriter {
public:
    [[nodiscard]] static aig::Circuit applyWireAlternative(const aig::Circuit& circuit,
                                                             const WireAlternative& alt);
};

}  // namespace satrar::rar