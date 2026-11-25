
#pragma once

#include "satrar/analysis/Fault.hpp"
#include "satrar/aig/Literal.hpp"

#include <string>
#include <variant>
#include <vector>

namespace satrar::rar {

struct Attachment {
    analysis::PinFault target;
    aig::NodeId dominator = aig::kInvalidNodeId;
    std::string evidence;
};

struct WireAlternative {
    Attachment attachment;
    aig::Literal source;
};

struct GateAlternative {
    Attachment attachment;
    // The additional input is NOT(AND(conjunction, OR(disjunction))).
    // An empty disjunction omits that factor.
    std::vector<aig::Literal> conjunction;
    std::vector<aig::Literal> disjunction;
};

using Alternative = std::variant<WireAlternative, GateAlternative>;

}  // namespace satrar::rar
