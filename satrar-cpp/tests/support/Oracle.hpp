// Independent exhaustive-simulation helpers shared by the oracle tests. They deliberately do not
// use FaultContext, SatRarEngine, the CNF encoder or any SAT solver.
#pragma once

#include "satrar/aig/Circuit.hpp"

#include <unordered_map>
#include <vector>

namespace oracle {

using satrar::aig::Circuit;
using satrar::aig::Literal;
using satrar::aig::NodeId;

using Values = std::unordered_map<std::uint32_t, bool>;

inline bool read(const Values& value, Literal lit) {
    if (lit.isConstant()) return lit.valueIfConstant();
    const bool v = value.at(lit.variable());
    return lit.inverted() ? !v : v;
}

/// All AIG variable values under PI vector \p mask (bit i = i-th primary input).
inline Values simulate(const Circuit& c, unsigned mask, const std::vector<NodeId>& order) {
    Values value;
    for (std::size_t i = 0; i < c.primaryInputIds().size(); ++i) {
        value[c.nodes()[c.primaryInputIds()[i]].aagVar] = ((mask >> i) & 1u) != 0;
    }
    for (const auto id : order) {
        const auto& node = c.nodes()[id];
        if (node.isAnd()) {
            value[node.aagVar] = read(value, node.andInputs().left) && read(value, node.andInputs().right);
        }
    }
    value[0] = false;
    return value;
}

inline std::vector<bool> outputsOf(const Circuit& c, unsigned mask, const std::vector<NodeId>& order) {
    const auto value = simulate(c, mask, order);
    std::vector<bool> out;
    for (const auto po : c.primaryOutputs()) out.push_back(read(value, po));
    return out;
}

inline bool equivalent(const Circuit& a, const Circuit& b) {
    const auto oa = a.topologicalOrder();
    const auto ob = b.topologicalOrder();
    if (a.primaryInputIds().size() != b.primaryInputIds().size() ||
        a.primaryOutputs().size() != b.primaryOutputs().size()) {
        return false;
    }
    for (unsigned mask = 0; mask < (1u << a.primaryInputIds().size()); ++mask) {
        if (outputsOf(a, mask, oa) != outputsOf(b, mask, ob)) return false;
    }
    return true;
}

/// PI vectors on which some PO of \p bad differs from \p good.
inline std::vector<unsigned> detecting(const Circuit& good, const Circuit& bad) {
    const auto og = good.topologicalOrder();
    const auto ob = bad.topologicalOrder();
    std::vector<unsigned> result;
    for (unsigned mask = 0; mask < (1u << good.primaryInputIds().size()); ++mask) {
        if (outputsOf(good, mask, og) != outputsOf(bad, mask, ob)) result.push_back(mask);
    }
    return result;
}

}  // namespace oracle
