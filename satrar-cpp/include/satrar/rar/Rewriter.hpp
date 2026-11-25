
#pragma once

#include "satrar/aig/Circuit.hpp"
#include "satrar/rar/Alternative.hpp"

namespace satrar::rar {

class Rewriter {
public:
    [[nodiscard]] static aig::Circuit addAlternative(const aig::Circuit& circuit, const Alternative& alt);
    [[nodiscard]] static aig::Circuit removeTarget(const aig::Circuit& added, analysis::PinFault target);
    [[nodiscard]] static aig::Circuit applyWireAlternative(const aig::Circuit& circuit, const WireAlternative& alt);
    [[nodiscard]] static aig::Circuit removeTargetAfterWire(const aig::Circuit& circuit, const WireAlternative& alt);
};

}  // namespace satrar::rar
