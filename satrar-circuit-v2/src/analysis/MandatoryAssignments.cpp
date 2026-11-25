#include "satrar/analysis/MandatoryAssignments.hpp"

namespace satrar::analysis {

void MandatoryAssignments::clear() {
    items_.clear();
    index_.clear();
}

void MandatoryAssignments::add(aig::Literal lit, std::uint32_t level, bool fromPropagation) {
    const auto key = lit.raw();
    const auto it = index_.find(key);
    if (it == index_.end()) {
        index_.emplace(key, items_.size());
        items_.push_back(MandatoryAssignment{lit, level, fromPropagation});
        return;
    }
    auto& slot = items_[it->second];
    slot.contextLevel = level;
    slot.fromPropagation = fromPropagation;
}

std::optional<bool> MandatoryAssignments::valueOf(aig::Literal lit) const {
    if (index_.count(lit.raw()) != 0) {
        return true;
    }
    if (!lit.isConstant() && index_.count((~lit).raw()) != 0) {
        return false;
    }
    return std::nullopt;
}

}  // namespace satrar::analysis
