#pragma once

#include "satrar/aig/Literal.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace satrar::analysis {

struct MandatoryAssignment {
    aig::Literal lit;
    std::uint32_t contextLevel = 0;
    bool fromPropagation = true;
};

class MandatoryAssignments {
public:
    void clear();
    void add(aig::Literal lit, std::uint32_t level, bool fromPropagation);
    [[nodiscard]] const std::vector<MandatoryAssignment>& items() const noexcept { return items_; }
    [[nodiscard]] std::optional<bool> valueOf(aig::Literal lit) const;

private:
    std::vector<MandatoryAssignment> items_;
    std::unordered_map<std::uint32_t, std::size_t> index_;
};

}  // namespace satrar::analysis
