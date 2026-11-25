#pragma once

#include "satrar/aig/WireRef.hpp"

#include <cstdint>

namespace satrar::analysis {

/// Stuck-at value at the modeled fault site.
enum class StuckAt : std::uint8_t { Zero = 0, One = 1 };

/// Fault on a specific receiving input pin (wire). Does not alter other fanouts of the driver.
struct PinFault final {
    aig::WireRef wire;
    StuckAt stuckAt = StuckAt::Zero;
};

/// Fault on the output of a dominator AND gate (distinct fault-test context from pin fault).
struct DominatorOutputFault final {
    aig::NodeId dominatorNode = aig::kInvalidNodeId;
    StuckAt stuckAt = StuckAt::Zero;
};

}  // namespace satrar::analysis