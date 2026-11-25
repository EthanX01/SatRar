
#include "satrar/rar/CircuitRewriter.hpp"
#include "satrar/rar/Rewriter.hpp"

namespace satrar::rar {

aig::Circuit CircuitRewriter::applyWireAlternative(const aig::Circuit& circuit,
                                                   const WireAlternative& alt) {
    return Rewriter::applyWireAlternative(circuit, alt);
}

}  // namespace satrar::rar
