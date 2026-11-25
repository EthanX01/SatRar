#pragma once

#include "satrar/sat/DimacsLiteral.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace satrar::analysis {

/// One entry from BCP under current assumptions (not a full SAT model).
struct MandatoryAssignment {
    satrar::sat::DimacsLiteral lit = 0;
    bool positivePolarityTrue = false;  /// DIMACS literal evaluates true under assignment
    std::uint32_t contextLevel = 0;
    bool fromPropagation = true;
};

class MandatoryAssignments {
public:
    void clear();

    void addAssumption(satrar::sat::DimacsLiteral lit, std::uint32_t contextLevel);
    void ingestPropagated(std::vector<satrar::sat::DimacsLiteral> propagated, std::uint32_t contextLevel);

    [[nodiscard]] const std::vector<MandatoryAssignment>& items() const noexcept { return items_; }
    [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }

    [[nodiscard]] MandatoryAssignments snapshot() const;
    void restoreSnapshot(const MandatoryAssignments& snap);

    [[nodiscard]] std::optional<bool> valueOf(satrar::sat::DimacsLiteral lit) const;

private:
    std::vector<MandatoryAssignment> items_;
    std::unordered_map<int, std::size_t> indexByLit_;

    void upsert(satrar::sat::DimacsLiteral lit, bool polTrue, std::uint32_t level, bool fromPropagation);
};

}  // namespace satrar::analysis