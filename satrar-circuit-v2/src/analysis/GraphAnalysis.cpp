
#include "satrar/analysis/GraphAnalysis.hpp"

#include <algorithm>
#include <iterator>

namespace satrar::analysis {

GraphAnalysis::GraphAnalysis(const aig::Circuit& circuit) : circuit_(circuit) {}

std::unordered_set<aig::NodeId> GraphAnalysis::faninCone(aig::NodeId start) const {
    std::unordered_set<aig::NodeId> visited;
    std::vector<aig::NodeId> todo{start};
    while (!todo.empty()) {
        const auto id = todo.back();
        todo.pop_back();
        if (id < circuit_.nodes().size() && visited.insert(id).second) {
            for (const auto wire : circuit_.faninWires(id)) {
                todo.push_back(resolveDriverNode(circuit_.driverLiteral(wire)));
            }
        }
    }
    return visited;
}

std::unordered_set<aig::NodeId> GraphAnalysis::fanoutCone(aig::NodeId start) const {
    std::unordered_set<aig::NodeId> visited;
    std::vector<aig::NodeId> todo{start};
    while (!todo.empty()) {
        const auto id = todo.back();
        todo.pop_back();
        if (id < circuit_.nodes().size() && visited.insert(id).second) {
            for (const auto wire : circuit_.fanoutWires(id)) {
                todo.push_back(wire.destination);
            }
        }
    }
    return visited;
}

std::vector<aig::NodeId> GraphAnalysis::dominatorsForWire(aig::WireRef target) const {
    return target.valid() && target.inputPin < 2 ? outputPostdominators(target.destination)
                                                : std::vector<aig::NodeId>{};
}

std::vector<aig::NodeId> GraphAnalysis::outputPostdominators(aig::NodeId start) const {
    if (start >= circuit_.nodes().size()) {
        return {};
    }
    const auto sink = circuit_.nodes().size();
    if (postdomParents_.empty()) {
        const auto order = circuit_.topologicalOrder();
        std::vector<std::size_t> rank(sink + 1), parent(sink + 1, sink + 1);
        std::vector<bool> directOutput(sink);
        parent[sink] = sink;
        rank[sink] = order.size();
        for (std::size_t i = 0; i < order.size(); ++i) {
            rank[order[i]] = i;
        }
        for (const auto output : circuit_.primaryOutputs()) {
            directOutput.at(resolveDriverNode(output)) = true;
        }
        // Intersect successor paths to the common PO sink in reverse topological order.
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            const auto node = *it;
            auto common = directOutput[node] ? sink : sink + 1;
            for (const auto wire : circuit_.fanoutWires(node)) {
                auto successor = static_cast<std::size_t>(wire.destination);
                if (parent[successor] > sink) {
                    continue;
                }
                if (common > sink) {
                    common = successor;
                    continue;
                }
                while (common != successor) {
                    if (rank[common] < rank[successor]) {
                        common = parent[common];
                    } else {
                        successor = parent[successor];
                    }
                }
            }
            parent[node] = common;
        }
        postdomParents_ = std::move(parent);
    }

    std::vector<aig::NodeId> out;
    if (postdomParents_[start] <= sink) {
        for (auto node = static_cast<std::size_t>(start); node != sink; node = postdomParents_[node]) {
            if (circuit_.nodes()[node].isAnd()) {
                out.push_back(static_cast<aig::NodeId>(node));
            }
        }
    }
    return out;
}

bool GraphAnalysis::wouldCreateCycle(aig::NodeId source, aig::NodeId destination) const {
    return fanoutCone(destination).count(source) != 0;
}

aig::NodeId GraphAnalysis::resolveDriverNode(aig::Literal lit) const {
    return lit.isConstant() ? (lit.valueIfConstant() ? circuit_.constTrueId() : circuit_.constFalseId())
                            : circuit_.findNodeByAigVar(lit.variable());
}

bool GraphAnalysis::wouldBypassWireCreateCycle(aig::WireRef attach, aig::WireRef source) const {
    if (!attach.valid() || !source.valid()) {
        return true;
    }
    return wouldCreateCycle(resolveDriverNode(circuit_.driverLiteral(source)), attach.destination);
}

}  // namespace satrar::analysis
