#pragma once

#include "satrar/aig/Circuit.hpp"
#include "satrar/aig/WireRef.hpp"

#include <unordered_set>
#include <vector>

namespace satrar::analysis {

/// Structural helpers for cones, dominators, and combinational cycle checks.
class GraphAnalysis {
public:
    explicit GraphAnalysis(const aig::Circuit& circuit);

    [[nodiscard]] std::unordered_set<aig::NodeId> faninCone(aig::NodeId start) const;
    [[nodiscard]] std::unordered_set<aig::NodeId> fanoutCone(aig::NodeId start) const;
    [[nodiscard]] std::vector<aig::NodeId> dominatorsForWire(aig::WireRef target) const;
    [[nodiscard]] std::vector<aig::NodeId> outputPostdominators(aig::NodeId start) const;
    [[nodiscard]] bool wouldCreateCycle(aig::NodeId sourceDriver, aig::NodeId newDestination) const;

    /// RAR wire bypass: sibling on the same AND never introduces a new edge; cross-gate uses fanout reachability.
    [[nodiscard]] bool wouldBypassWireCreateCycle(aig::WireRef attachAt,
                                                  aig::WireRef candidateSourceWire) const;

private:
    [[nodiscard]] aig::NodeId resolveDriverNode(aig::Literal lit) const;
    const aig::Circuit& circuit_;
    mutable std::vector<std::size_t> postdomParents_;

};

}  // namespace satrar::analysis
