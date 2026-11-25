#include "satrar/solver/GateNet.hpp"

#include <stdexcept>

namespace satrar::solver {

GateNet::GateNet(const aig::Circuit& circuit) {
    aigOf_.push_back(0);
    isPi_.push_back(0);
    for (const auto id : circuit.topologicalOrder()) {
        const auto& node = circuit.nodes().at(id);
        if (!node.isPrimaryInput() && !node.isAnd()) {
            continue;
        }
        denseOf_.emplace(node.aagVar, static_cast<std::uint32_t>(aigOf_.size()));
        if (node.isPrimaryInput()) {
            pis_.push_back(static_cast<std::uint32_t>(aigOf_.size()));
        }
        aigOf_.push_back(node.aagVar);
        isPi_.push_back(node.isPrimaryInput() ? 1 : 0);
    }
    incident_.assign(aigOf_.size(), {});
    for (const auto id : circuit.topologicalOrder()) {
        const auto& node = circuit.nodes().at(id);
        if (!node.isAnd()) {
            continue;
        }
        Gate gate;
        gate.node = id;
        gate.aigVar = node.aagVar;
        gate.out = denseOfAig(node.aagVar);
        gate.left = pack(node.andInputs().left);
        gate.right = pack(node.andInputs().right);
        const auto index = static_cast<std::uint32_t>(gates_.size());
        auto add = [&](std::uint32_t var) {
            auto& list = incident_[var];
            if (list.empty() || list.back() != index) {
                list.push_back(index);
            }
            ++indexTouches_;
        };
        add(gate.out);
        add(gate.left.var);
        if (gate.right.var != gate.left.var) {
            add(gate.right.var);
        }
        gates_.push_back(gate);
    }
}

std::uint32_t GateNet::denseOfAig(std::uint32_t aigVar) const {
    const auto it = denseOf_.find(aigVar);
    if (it == denseOf_.end()) {
        throw std::out_of_range("AIG variable is not a signal in this net");
    }
    return it->second;
}

aig::Literal GateNet::literalOf(std::uint32_t dense, bool negated) const {
    if (dense == 0) {
        return aig::Literal(negated ? 1u : 0u);
    }
    const auto positive = aig::Literal::fromVariable(aigOf_.at(dense));
    return negated ? ~positive : positive;
}

GateNet::Edge GateNet::pack(aig::Literal lit) const {
    if (lit.isConstant()) {
        return Edge{0, lit.valueIfConstant()};
    }
    return Edge{denseOfAig(lit.variable()), lit.inverted()};
}

}  // namespace satrar::solver
