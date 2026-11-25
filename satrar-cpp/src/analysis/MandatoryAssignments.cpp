#include "satrar/analysis/MandatoryAssignments.hpp"

namespace satrar::analysis {

void MandatoryAssignments::clear() {
    items_.clear();
    indexByLit_.clear();
}

void MandatoryAssignments::upsert(satrar::sat::DimacsLiteral lit, bool /*polTrue*/, std::uint32_t level,
                                  bool fromPropagation) {
    const int key = lit;
    const auto it = indexByLit_.find(key);
    if (it == indexByLit_.end()) {
        indexByLit_[key] = items_.size();
        items_.push_back(MandatoryAssignment{lit, true, level, fromPropagation});
        return;
    }
    MandatoryAssignment& slot = items_[it->second];
    slot.contextLevel = level;
    slot.fromPropagation = fromPropagation;
}

void MandatoryAssignments::addAssumption(satrar::sat::DimacsLiteral lit, std::uint32_t contextLevel) {
    upsert(lit, true, contextLevel, false);
}

void MandatoryAssignments::ingestPropagated(std::vector<satrar::sat::DimacsLiteral> propagated,
                                            std::uint32_t contextLevel) {
    for (const satrar::sat::DimacsLiteral lit : propagated) {
        upsert(lit, true, contextLevel, true);
    }
}

MandatoryAssignments MandatoryAssignments::snapshot() const {
    return *this;
}

void MandatoryAssignments::restoreSnapshot(const MandatoryAssignments& snap) {
    items_ = snap.items_;
    indexByLit_ = snap.indexByLit_;
}

std::optional<bool> MandatoryAssignments::valueOf(satrar::sat::DimacsLiteral lit) const {
    if (indexByLit_.count(lit) != 0) {
        return true;
    }
    if (indexByLit_.count(-lit) != 0) {
        return false;
    }
    return std::nullopt;
}

}  // namespace satrar::analysis