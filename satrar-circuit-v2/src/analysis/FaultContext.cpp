#include "satrar/analysis/FaultContext.hpp"

#include <chrono>
#include <stdexcept>

namespace satrar::analysis {
namespace {

aig::Literal makeAnd(aig::Circuit& circuit, std::uint32_t& next, aig::Literal left, aig::Literal right) {
    circuit.addAndGate(++next, left, right);
    return aig::Literal::fromVariable(next);
}

}  // namespace

FaultContext::FaultContext(aig::Circuit circuit)
    : circuit_(std::move(circuit)), graph_(circuit_), net_(circuit_), session_(net_) {}

void FaultContext::resetSolver() {
    session_.forgetLearned();
    requirements_.clear();
    status_ = FaultStatus::NoFault;
}

void FaultContext::setPinFault(PinFault fault) { fault_ = fault; }

void FaultContext::setDominatorOutputFault(DominatorOutputFault fault) { fault_ = fault; }

void FaultContext::clearFault() {
    fault_ = std::monostate{};
    requirements_.clear();
    status_ = FaultStatus::NoFault;
    (void)session_.setAssumptions({});
}

std::optional<aig::Literal> FaultContext::excitation() const {
    if (const auto* pin = std::get_if<PinFault>(&fault_)) {
        const auto edge = circuit_.driverLiteral(pin->wire);
        return pin->stuckAt == StuckAt::One ? ~edge : edge;
    }
    if (const auto* output = std::get_if<DominatorOutputFault>(&fault_)) {
        const auto positive = aig::Literal::fromVariable(circuit_.nodes().at(output->dominatorNode).aagVar);
        return output->stuckAt == StuckAt::One ? ~positive : positive;
    }
    return std::nullopt;
}

FaultContext::Miter FaultContext::buildMiter() const {
    Miter miter;
    const auto* pin = std::get_if<PinFault>(&fault_);
    const auto start = pin ? pin->wire.destination : std::get<DominatorOutputFault>(fault_).dominatorNode;
    const auto stuck = pin ? pin->stuckAt : std::get<DominatorOutputFault>(fault_).stuckAt;
    const auto affected = graph_.fanoutCone(start);

    miter.circuit.addConstantNode(false);
    miter.circuit.addConstantNode(true);
    for (const auto id : circuit_.primaryInputIds()) {
        const auto& node = circuit_.nodes()[id];
        miter.circuit.addPrimaryInput(node.aagVar, node.symbol);
        const auto lit = aig::Literal::fromVariable(node.aagVar);
        miter.goodOf.emplace(node.aagVar, lit);
    }

    std::vector<aig::Literal> outputs;
    for (const auto output : circuit_.primaryOutputs()) {
        if (output.isConstant()) {
            continue;
        }
        const auto id = circuit_.findNodeByAigVar(output.variable());
        if (affected.count(id) != 0) {
            outputs.push_back(output);
        }
    }
    if (outputs.empty()) {
        return miter;
    }

    std::unordered_map<std::uint32_t, aig::Literal> badOf = miter.goodOf;
    auto edgeLit = [&](aig::Literal edge, bool faulty) {
        if (edge.isConstant()) {
            return edge;
        }
        const auto base = (faulty ? badOf : miter.goodOf).at(edge.variable());
        return edge.inverted() ? ~base : base;
    };
    auto next = circuit_.stats().maxAigVar;
    for (const auto id : circuit_.topologicalOrder()) {
        if (!circuit_.nodes()[id].isAnd()) {
            continue;
        }
        const auto& node = circuit_.nodes()[id];
        const auto& in = node.andInputs();
        const auto good = makeAnd(miter.circuit, next, edgeLit(in.left, false), edgeLit(in.right, false));
        miter.goodOf[node.aagVar] = good;
        if (affected.count(id) == 0) {
            badOf[node.aagVar] = good;
            continue;
        }
        if (!pin && id == start) {
            badOf[node.aagVar] = aig::Literal(static_cast<std::uint32_t>(stuck));
            continue;
        }
        const auto left = pin && id == start && pin->wire.inputPin == 0
                              ? aig::Literal(static_cast<std::uint32_t>(stuck))
                              : edgeLit(in.left, true);
        const auto right = pin && id == start && pin->wire.inputPin == 1
                               ? aig::Literal(static_cast<std::uint32_t>(stuck))
                               : edgeLit(in.right, true);
        badOf[node.aagVar] = makeAnd(miter.circuit, next, left, right);
    }

    std::vector<aig::Literal> diffs;
    for (const auto output : outputs) {
        const auto a = edgeLit(output, false);
        const auto b = edgeLit(output, true);
        const auto left = makeAnd(miter.circuit, next, a, ~b);
        const auto right = makeAnd(miter.circuit, next, ~a, b);
        diffs.push_back(~makeAnd(miter.circuit, next, ~left, ~right));
    }
    auto any = diffs.front();
    for (std::size_t i = 1; i < diffs.size(); ++i) {
        any = ~makeAnd(miter.circuit, next, ~any, ~diffs[i]);
    }
    miter.detect = any;
    miter.observable = true;
    miter.circuit.addPrimaryOutput(any);
    miter.circuit.validate();
    return miter;
}

bool FaultContext::project(const Miter& miter) {
    if (!miter.observable) {
        return false;
    }
    const solver::GateNet net(miter.circuit);
    solver::SolverSession exact(net);
    if (exact.setAssumptions({miter.detect}) == solver::QueryState::Conflict) {
        return false;
    }
    for (const auto& [var, good] : miter.goodOf) {
        const auto value = exact.value(good);
        if (!value) {
            continue;
        }
        requirements_.push_back(*value ? aig::Literal::fromVariable(var) : ~aig::Literal::fromVariable(var));
    }
    return true;
}

MandatoryAssignments FaultContext::computeMandatoryAssignments(const std::vector<aig::Literal>& extra) {
    requirements_.clear();
    status_ = FaultStatus::NoFault;
    const auto excite = excitation();
    if (!excite) {
        (void)session_.setAssumptions({});
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
        std::vector<aig::Literal> unaffected;
        for (const auto wire : circuit_.faninWires(gate)) {
            const auto edge = circuit_.driverLiteral(wire);
            const bool changed = pin && gate == start
                                     ? wire == pin->wire
                                     : !edge.isConstant() &&
                                           affected.count(circuit_.findNodeByAigVar(edge.variable())) != 0;
            if (!changed) {
                unaffected.push_back(edge);
            }
        }
        if (unaffected.size() == 1) {
            requirements_.push_back(unaffected.front());
        } else {
            reconvergent = true;
        }
    }
    requirements_.push_back(*excite);
    ++profile_.computations;
    if (reconvergent) {
        const auto began = std::chrono::steady_clock::now();
        const bool consistent = project(buildMiter());
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
    const auto state = session_.setAssumptions(assumptions);
    profile_.propagationSeconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    if (state == solver::QueryState::Conflict) {
        status_ = FaultStatus::Conflict;
        return {};
    }
    status_ = reconvergent ? FaultStatus::NeedsExactTest : FaultStatus::Consistent;
    return snapshot();
}

MandatoryAssignments FaultContext::snapshot() const {
    MandatoryAssignments ma;
    for (const auto lit : session_.trail()) {
        ma.add(lit, session_.level(lit), !session_.isDecision(lit));
    }
    return ma;
}

ExactAnswer FaultContext::solveDetection(const std::vector<aig::Literal>& extra, solver::Budget& budget,
                                         bool cdcl) const {
    ExactAnswer answer;
    const auto miter = buildMiter();
    if (!miter.observable) {
        answer.state = solver::SolveState::Unsat;
        answer.redundantFault = true;
        answer.reason = "unobservable";
        return answer;
    }
    std::vector<aig::Literal> assumptions{miter.detect};
    for (const auto lit : extra) {
        if (lit.isConstant()) {
            assumptions.push_back(lit);
            continue;
        }
        const auto it = miter.goodOf.find(lit.variable());
        if (it == miter.goodOf.end()) {
            throw std::invalid_argument("good view has no signal for AIG variable " +
                                        std::to_string(lit.variable()));
        }
        assumptions.push_back(lit.inverted() ? ~it->second : it->second);
    }
    const solver::GateNet net(miter.circuit);
    solver::SolverSession exact(net);
    exact.setCdcl(cdcl);
    const auto solved = exact.solve(assumptions, budget);
    answer.state = solved.state;
    answer.reason = solved.reason;
    answer.primaryInputs = solved.primaryInputs;
    answer.redundantFault = extra.empty() && solved.state == solver::SolveState::Unsat;
    return answer;
}

ExactAnswer FaultContext::checkMandatory(aig::Literal lit, solver::Budget& budget, bool cdcl) const {
    auto base = solveDetection({}, budget, cdcl);
    if (base.state == solver::SolveState::Unsat) {
        base.redundantFault = true;
        base.mandatory = false;
        return base;
    }
    if (base.state != solver::SolveState::Sat) {
        return base;
    }
    auto blocked = solveDetection({~lit}, budget, cdcl);
    blocked.mandatory = blocked.state == solver::SolveState::Unsat;
    blocked.redundantFault = false;
    return blocked;
}

}  // namespace satrar::analysis
