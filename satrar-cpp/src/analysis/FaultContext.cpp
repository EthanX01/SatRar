
#include "satrar/analysis/FaultContext.hpp"

#include "satrar/analysis/GraphAnalysis.hpp"
#include "satrar/sat/CnfEncoder.hpp"
#include "satrar/sat/CnfConstants.hpp"
#include "satrar/sat/MiterBuilder.hpp"

#include <algorithm>
#include <chrono>

namespace satrar::analysis {

FaultContext::FaultContext(aig::Circuit circuit)
    : circuit_(std::move(circuit)), graph_(circuit_), propagator_(circuit_) {}

void FaultContext::resetSolver() {
    propagator_.forgetLearned();
    requirements_.clear();
    status_ = FaultStatus::NoFault;
}

void FaultContext::rollbackFaultContext() {
    propagator_.setAssumptions({});
    requirements_.clear();
    status_ = FaultStatus::NoFault;
}

void FaultContext::setPinFault(PinFault fault) { fault_ = fault; }
void FaultContext::setDominatorOutputFault(DominatorOutputFault fault) { fault_ = fault; }
void FaultContext::clearFault() {
    fault_ = std::monostate{};
    rollbackFaultContext();
}

std::optional<sat::DimacsLiteral> FaultContext::stuckAtAssumption() const {
    if (const auto* pin = std::get_if<PinFault>(&fault_)) {
        const auto literal = sat::CnfEncoder::dimacsLitForAig(cnf(), circuit_.driverLiteral(pin->wire));
        return pin->stuckAt == StuckAt::One ? -*literal : *literal;
    }
    if (const auto* output = std::get_if<DominatorOutputFault>(&fault_)) {
        const auto literal = sat::CnfEncoder::positiveLit(cnf(), circuit_.nodes().at(output->dominatorNode).aagVar);
        return output->stuckAt == StuckAt::One ? -*literal : *literal;
    }
    return std::nullopt;
}

aig::Circuit FaultContext::faultyCircuit() const {
    auto faulty = circuit_.clone();
    if (const auto* pin = std::get_if<PinFault>(&fault_)) {
        faulty.replaceAndFanin(pin->wire.destination, pin->wire.inputPin,
                               aig::Literal(static_cast<std::uint32_t>(pin->stuckAt)));
    } else if (const auto* output = std::get_if<DominatorOutputFault>(&fault_)) {
        faulty.substituteAigVar(circuit_.nodes().at(output->dominatorNode).aagVar,
                                aig::Literal(static_cast<std::uint32_t>(output->stuckAt)));
    } else {
        throw std::logic_error("no active fault");
    }
    return faulty;
}

sat::CnfFormula FaultContext::faultDetectionFormula(
    const std::vector<sat::DimacsLiteral>& extra) const {
    sat::CnfFormula miter;
    const auto good = sat::MiterBuilder::encode(circuit_, sat::MiterSide::A, &miter);
    const auto bad = sat::MiterBuilder::encode(faultyCircuit(), sat::MiterSide::B, &miter);
    (void)sat::MiterBuilder::buildXorMiter(good, bad, miter);
    for (const auto lit : extra) {
        const auto encoded = std::find_if(cnf().aigToSat().begin(), cnf().aigToSat().end(),
            [&](const auto& item) { return item.second == sat::dimacsVariable(lit); });
        if (encoded == cnf().aigToSat().end()) {
            throw std::invalid_argument("unknown fault assumption");
        }
        const auto found = good.signals.find(encoded->first);
        const auto mapped = found != good.signals.end() ? found->second
            : sat::dimacsFromVar(miter.satVarOfAig(encoded->first), true);
        miter.addClause({lit < 0 ? -mapped : mapped});
    }
    return miter;
}

bool FaultContext::projectFaultImplications(
    aig::NodeId start, const std::unordered_set<aig::NodeId>& affected) {
    using sat::DimacsLiteral;
    sat::CnfFormula miter;
    const auto constants = sat::bindAigConstants(miter);
    const auto* pin = std::get_if<PinFault>(&fault_);
    const auto stuck = pin ? pin->stuckAt : std::get<DominatorOutputFault>(fault_).stuckAt;
    const auto forced = sat::aigLiteralToDimacs(
        aig::Literal(static_cast<std::uint32_t>(stuck)), constants, miter);
    sat::MiterBuilder::EncodedCircuit good, bad;
    std::vector<aig::Literal> outputs;
    std::vector<aig::NodeId> todo;
    for (const auto output : circuit_.primaryOutputs()) {
        if (output.isConstant()) {
            continue;
        }
        const auto id = circuit_.findNodeByAigVar(output.variable());
        if (affected.count(id)) {
            outputs.push_back(output);
            todo.push_back(id);
        }
    }
    const auto literal = [&](aig::Literal edge, bool faulty) -> DimacsLiteral {
        if (edge.isConstant()) {
            return sat::aigLiteralToDimacs(edge, constants, miter);
        }
        const auto& signals = faulty ? bad.signals : good.signals;
        const auto lit = signals.at(edge.variable());
        return edge.inverted() ? -lit : lit;
    };
    const auto addAnd = [&](DimacsLiteral z, DimacsLiteral x, DimacsLiteral y) {
        miter.addClause({-z, x});
        miter.addClause({-z, y});
        miter.addClause({z, -x, -y});
    };
    std::vector<aig::NodeId> live;
    while (!todo.empty()) {
        const auto id = todo.back();
        todo.pop_back();
        const auto& node = circuit_.nodes()[id];
        if (good.signals.count(node.aagVar)) {
            continue;
        }
        live.push_back(id);
        const auto z = sat::dimacsFromVar(miter.newVariable(), true);
        good.signals.emplace(node.aagVar, z);
        const auto faulty = !affected.count(id) ? z : id == start && !pin
            ? forced : sat::dimacsFromVar(miter.newVariable(), true);
        bad.signals.emplace(node.aagVar, faulty);
        for (const auto wire : circuit_.faninWires(id)) {
            const auto edge = circuit_.driverLiteral(wire);
            if (!edge.isConstant()) {
                todo.push_back(circuit_.findNodeByAigVar(edge.variable()));
            }
        }
    }
    for (const auto id : live) {
        const auto& node = circuit_.nodes()[id];
        if (!node.isAnd()) {
            continue;
        }
        const auto& in = node.andInputs();
        const auto z = good.signals.at(node.aagVar);
        const auto faulty = bad.signals.at(node.aagVar);
        addAnd(z, literal(in.left, false), literal(in.right, false));
        if (z != faulty && (pin || id != start)) {
            addAnd(faulty, pin && id == start && pin->wire.inputPin == 0
                               ? forced : literal(in.left, true),
                           pin && id == start && pin->wire.inputPin == 1
                               ? forced : literal(in.right, true));
        }
    }
    for (const auto output : outputs) {
        good.primaryOutputLits.push_back(literal(output, false));
        bad.primaryOutputLits.push_back(literal(output, true));
    }
    (void)sat::MiterBuilder::buildXorMiter(good, bad, miter);
    CircuitPropagator exact(std::move(miter));
    if (!exact.propagate()) {
        return false;
    }
    for (const auto id : live) {
        const auto var = circuit_.nodes()[id].aagVar;
        if (const auto v = exact.value(good.signals.at(var))) {
            const auto lit = sat::CnfEncoder::positiveLit(cnf(), var).value();
            requirements_.push_back(*v ? lit : -lit);
        }
    }
    return true;
}

MandatoryAssignments FaultContext::computeMandatoryAssignments(
    const std::vector<sat::DimacsLiteral>& extra) {
    requirements_.clear();
    status_ = FaultStatus::NoFault;
    const auto excitation = stuckAtAssumption();
    if (!excitation) {
        rollbackFaultContext();
        return {};
    }
    const auto* pin = std::get_if<PinFault>(&fault_);
    const auto start = pin ? pin->wire.destination : std::get<DominatorOutputFault>(fault_).dominatorNode;
    const auto dominators = graph_.outputPostdominators(start);
    if (dominators.empty()) {
        status_ = FaultStatus::Conflict;
        return {};
    }
    const auto affected = graph_.fanoutCone(start);
    bool reconvergent = false;
    for (auto it = dominators.rbegin(); it != dominators.rend(); ++it) {
        const auto gate = *it;
        if (!pin && gate == start) {
            continue;
        }
        std::vector<sat::DimacsLiteral> unaffected;
        for (const auto wire : circuit_.faninWires(gate)) {
            const auto edge = circuit_.driverLiteral(wire);
            const bool changed = gate == start ? wire == pin->wire :
                !edge.isConstant() && affected.count(circuit_.findNodeByAigVar(edge.variable()));
            if (!changed) {
                unaffected.push_back(sat::CnfEncoder::dimacsLitForAig(cnf(), edge).value());
            }
        }
        if (unaffected.size() == 1) {
            requirements_.push_back(unaffected.front());
        } else {
            reconvergent = true;
        }
    }
    requirements_.push_back(*excitation);
    ++profile_.computations;
    if (reconvergent) {
        const auto began = std::chrono::steady_clock::now();
        const bool consistent = projectFaultImplications(start, affected);
        profile_.projectionSeconds +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        ++profile_.projections;
        if (!consistent) {
            status_ = FaultStatus::Conflict;
            return {};
        }
    }
    auto assumptions = requirements_;
    assumptions.insert(assumptions.end(), extra.begin(), extra.end());
    const auto began = std::chrono::steady_clock::now();
    propagator_.setAssumptions(assumptions);
    const bool propagated = propagator_.propagate();
    profile_.propagationSeconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    if (!propagated) {
        status_ = FaultStatus::Conflict;
        return {};
    }
    status_ = reconvergent ? FaultStatus::NeedsExactTest : FaultStatus::Consistent;
    return snapshot();
}

MandatoryAssignments FaultContext::snapshot() const {
    MandatoryAssignments ma;
    for (const auto lit : propagator_.trail()) {
        if (propagator_.reason(lit).empty()) {
            ma.addAssumption(lit, propagator_.level(lit));
        } else {
            ma.ingestPropagated({lit}, propagator_.level(lit));
        }
    }
    return ma;
}

std::optional<bool> FaultContext::impliedValue(sat::DimacsLiteral lit) const {
    return status_ == FaultStatus::Consistent || status_ == FaultStatus::NeedsExactTest
               ? propagator_.value(lit) : std::nullopt;
}

}  // namespace satrar::analysis
