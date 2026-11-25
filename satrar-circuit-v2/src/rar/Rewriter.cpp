
#include "satrar/rar/Rewriter.hpp"

#include "satrar/analysis/GraphAnalysis.hpp"

#include <algorithm>
#include <type_traits>
#include <unordered_map>

namespace satrar::rar {
namespace {

using aig::Literal;

Literal appendAnd(aig::Circuit& circuit, std::uint32_t& next, Literal x, Literal y) {
    if (next == 0x7fffffffu) {
        throw std::overflow_error("out of AAG variables");
    }
    circuit.addAndGate(++next, x, y);
    return Literal::fromVariable(next);
}

aig::Circuit simplify(const aig::Circuit& circuit) {
    aig::Circuit result;
    result.addConstantNode(false);
    result.addConstantNode(true);
    std::unordered_map<std::uint32_t, Literal> signals;
    const auto resolve = [&](Literal edge) {
        if (edge.isConstant()) {
            return edge;
        }
        const auto value = signals.at(edge.variable());
        return edge.inverted() ? ~value : value;
    };
    for (const auto id : circuit.primaryInputIds()) {
        const auto& pi = circuit.nodes()[id];
        result.addPrimaryInput(pi.aagVar, pi.symbol);
        signals.emplace(pi.aagVar, Literal::fromVariable(pi.aagVar));
    }
    std::unordered_set<aig::NodeId> live;
    const analysis::GraphAnalysis graph(circuit);
    for (const auto po : circuit.primaryOutputs()) {
        if (!po.isConstant()) {
            const auto cone = graph.faninCone(circuit.findNodeByAigVar(po.variable()));
            live.insert(cone.begin(), cone.end());
        }
    }

    for (const auto id : circuit.topologicalOrder()) {
        const auto& node = circuit.nodes()[id];
        if (!node.isAnd() || !live.count(id)) {
            continue;
        }
        const auto x = resolve(node.andInputs().left);
        const auto y = resolve(node.andInputs().right);
        Literal mapped(0);
        if (x == Literal(1) || x == y) {
            mapped = y;
        } else if (y == Literal(1)) {
            mapped = x;
        } else if (x != Literal(0) && y != Literal(0) && x != ~y) {
            result.addAndGate(node.aagVar, x, y);
            mapped = Literal::fromVariable(node.aagVar);
        }
        signals.emplace(node.aagVar, mapped);
    }
    for (std::size_t i = 0; i < circuit.primaryOutputs().size(); ++i) {
        result.addPrimaryOutput(resolve(circuit.primaryOutputs()[i]), circuit.outputSymbols()[i]);
    }
    if (const auto comment = circuit.comment()) {
        result.setComment(*comment);
    }
    result.validate();
    return result;
}

}  // namespace

aig::Circuit Rewriter::addAlternative(const aig::Circuit& circuit, const Alternative& alt) {
    auto result = circuit.clone();
    auto next = circuit.stats().maxAigVar;
    std::visit([&](const auto& candidate) {
        const auto& attachment = candidate.attachment;
        const auto dom = attachment.dominator;
        const auto& node = circuit.nodes().at(dom);
        const auto doms = analysis::GraphAnalysis(circuit).dominatorsForWire(attachment.target.wire);
        if (!node.isAnd() || std::find(doms.begin(), doms.end(), dom) == doms.end()) {
            throw std::invalid_argument("attachment must be a target postdominator");
        }
        const auto safe = [&](Literal edge) {
            if (!edge.isConstant()) {
                const auto source = circuit.findNodeByAigVar(edge.variable());
                if (source == aig::kInvalidNodeId ||
                    analysis::GraphAnalysis(circuit).wouldCreateCycle(source, dom)) {
                    throw std::invalid_argument("unknown or cyclic alternative signal");
                }
            }
            return edge;
        };
        Literal condition;
        if constexpr (std::is_same_v<std::decay_t<decltype(candidate)>, WireAlternative>) {
            condition = safe(candidate.source);
        } else {
            if (candidate.conjunction.empty()) {
                throw std::invalid_argument("gate candidate has no operands");
            }
            auto term = safe(candidate.conjunction.front());
            for (std::size_t i = 1; i < candidate.conjunction.size(); ++i) {
                term = appendAnd(result, next, term, safe(candidate.conjunction[i]));
            }
            if (!candidate.disjunction.empty()) {
                auto complement = ~safe(candidate.disjunction.front());
                for (std::size_t i = 1; i < candidate.disjunction.size(); ++i) {
                    complement = appendAnd(result, next, complement, ~safe(candidate.disjunction[i]));
                }
                term = appendAnd(result, next, term, ~complement);
            }
            condition = ~term;
        }
        const auto wrapper = appendAnd(result, next, Literal::fromVariable(node.aagVar), condition);
        result.substituteAigVar(node.aagVar, wrapper, result.findNodeByAigVar(wrapper.variable()));
    }, alt);
    result.validate();
    return result;
}

aig::Circuit Rewriter::removeTarget(const aig::Circuit& added, analysis::PinFault target) {
    auto tied = added.clone();
    tied.replaceAndFanin(target.wire.destination, target.wire.inputPin,
                        Literal(static_cast<std::uint32_t>(target.stuckAt)));
    // The first pass folds gates; the second sweeps disconnected operands of folded gates.
    return simplify(simplify(tied));
}

aig::Circuit Rewriter::applyWireAlternative(const aig::Circuit& circuit, const WireAlternative& alt) {
    return addAlternative(circuit, alt);
}

aig::Circuit Rewriter::removeTargetAfterWire(const aig::Circuit& circuit, const WireAlternative& alt) {
    return removeTarget(addAlternative(circuit, alt), alt.attachment.target);
}

}  // namespace satrar::rar
