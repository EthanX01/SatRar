#pragma once

#include "satrar/aig/Node.hpp"

#include <cstdint>
#include <functional>

namespace satrar::aig {

/// Identifies one destination input pin (a wire), not merely the driving node.
struct WireRef final {
    NodeId destination = kInvalidNodeId;
    std::uint8_t inputPin = 0;  /// 0 or 1 for AND gates

    [[nodiscard]] bool valid() const noexcept { return destination != kInvalidNodeId; }

    [[nodiscard]] bool operator==(WireRef other) const noexcept {
        return destination == other.destination && inputPin == other.inputPin;
    }

    [[nodiscard]] bool operator!=(WireRef other) const noexcept { return !(*this == other); }
};

struct WireRefHash {
    std::size_t operator()(WireRef w) const noexcept {
        return std::hash<std::uint32_t>{}(w.destination) ^ (std::hash<std::uint8_t>{}(w.inputPin) << 1u);
    }
};

}  // namespace satrar::aig