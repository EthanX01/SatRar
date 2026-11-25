#include "satrar/aig/AagParser.hpp"
#include "satrar/aig/Circuit.hpp"
#include "satrar/analysis/FaultContext.hpp"
#include "satrar/solver/GateNet.hpp"
#include "satrar/solver/SolverSession.hpp"

#include <exception>
#include <cstdio>
#include <filesystem>
#include <map>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d (%s)\n", __FILE__, __LINE__, #cond);            \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

using namespace satrar;
using aig::Literal;

namespace {

std::vector<bool> bitsOf(unsigned mask, std::size_t width) {
    std::vector<bool> bits(width);
    for (std::size_t i = 0; i < width; ++i) {
        bits[i] = ((mask >> i) & 1u) != 0;
    }
    return bits;
}

bool replay(const aig::Circuit& circuit, const std::vector<bool>& model, const std::vector<Literal>& assumptions) {
    if (model.size() != circuit.primaryInputIds().size()) {
        return false;
    }
    std::vector<bool> copy(model.begin(), model.end());
    (void)circuit.evaluate(copy);
    std::map<std::uint32_t, bool> value;
    for (std::size_t i = 0; i < copy.size(); ++i) {
        value[circuit.nodes()[circuit.primaryInputIds()[i]].aagVar] = copy[i];
    }
    for (const auto id : circuit.topologicalOrder()) {
        const auto& node = circuit.nodes()[id];
        if (!node.isAnd()) {
            continue;
        }
        const auto read = [&](Literal edge) {
            return edge.isConstant() ? edge.valueIfConstant() : value.at(edge.variable()) != edge.inverted();
        };
        value[node.aagVar] = read(node.andInputs().left) && read(node.andInputs().right);
    }
    for (const auto lit : assumptions) {
        const bool got = lit.isConstant() ? lit.valueIfConstant() : value.at(lit.variable()) != lit.inverted();
        if (!got) {
            return false;
        }
    }
    return true;
}

solver::SolveResult solve(const aig::Circuit& circuit, const std::vector<Literal>& assumptions, bool cdcl,
                          solver::Budget budget) {
    solver::GateNet net(circuit);
    solver::SolverSession session(net);
    session.setCdcl(cdcl);
    return session.solve(assumptions, budget);
}

}  // namespace

int main() try {
    aig::Circuit circuit;
    circuit.addConstantNode(false);
    circuit.addConstantNode(true);
    circuit.addPrimaryInput(1);
    circuit.addPrimaryInput(2);
    circuit.addAndGate(3, Literal::fromVariable(1), Literal::fromVariable(2));
    circuit.addPrimaryOutput(Literal::fromVariable(3));
    const Literal x = Literal::fromVariable(1);
    const Literal y = Literal::fromVariable(2);
    const Literal z = Literal::fromVariable(3);
    solver::Budget budget;
    budget.decisionLimit = 20;
    for (const bool cdcl : {false, true}) {
        const auto sat = solve(circuit, {z}, cdcl, budget);
        CHECK(sat.state == solver::SolveState::Sat);
        CHECK(replay(circuit, sat.primaryInputs, {z}));
        for (const auto& clause : sat.learned) {
            for (unsigned mask = 0; mask < 4; ++mask) {
                const bool xv = (mask & 1u) != 0;
                const bool yv = (mask & 2u) != 0;
                const bool zv = xv && yv;
                bool holds = clause.empty();
                for (const auto lit : clause) {
                    const bool bit = lit.variable() == 1 ? xv : lit.variable() == 2 ? yv : zv;
                    holds = holds || (bit != lit.inverted());
                }
                CHECK(holds);
            }
        }
        CHECK(sat.primaryInputs.size() == 2 && sat.primaryInputs[0] && sat.primaryInputs[1]);
        const auto unsat = solve(circuit, {z, ~x}, cdcl, budget);
        CHECK(unsat.state == solver::SolveState::Unsat);
        const auto again = solve(circuit, {~z}, cdcl, budget);
        CHECK(again.state == solver::SolveState::Sat);
        CHECK(replay(circuit, again.primaryInputs, {~z}));
    }
    budget.decisionLimit = 0;
    const auto unknown = solve(circuit, {}, false, budget);
    CHECK(unknown.state == solver::SolveState::Unknown);
    CHECK(unknown.reason == "decision_limit");

    const auto fig1 = aig::AagParser::parseFile(
        (std::filesystem::path(__FILE__).parent_path() / "fixtures/paper/fig1.aag").string());
    analysis::FaultContext context(fig1.clone());
    const auto g7 = fig1.findNodeByAigVar(13);
    context.setPinFault({{g7, 0}, analysis::StuckAt::One});
    solver::Budget exactBudget;
    exactBudget.decisionLimit = 200;
    unsigned satCount = 0;
    for (const bool cdcl : {false, true}) {
        const auto answer = context.solveDetection({}, exactBudget, cdcl);
        CHECK(answer.state == solver::SolveState::Sat);
        CHECK(replay(fig1, answer.primaryInputs, {}));
        auto bad = fig1.clone();
        bad.replaceAndFanin(g7, 0, Literal(1));
        CHECK(fig1.evaluate(answer.primaryInputs) != bad.evaluate(answer.primaryInputs));
        ++satCount;
    }
    CHECK(satCount == 2);
    const auto mandatory = context.checkMandatory(~Literal(25), exactBudget, true);
    CHECK(mandatory.state == solver::SolveState::Unsat);
    CHECK(mandatory.mandatory);
    const auto notMa = context.checkMandatory(Literal(16), exactBudget, false);
    CHECK(notMa.state == solver::SolveState::Sat);
    CHECK(!notMa.mandatory);

    context.setDominatorOutputFault({fig1.findNodeByAigVar(15), analysis::StuckAt::Zero});
    const auto blocked = context.solveDetection({~Literal(23)}, exactBudget, true);
    CHECK(blocked.state == solver::SolveState::Unsat);

    std::fprintf(stderr, "test_exact: ok\n");
    return 0;
} catch (const std::exception& ex) {
    std::fprintf(stderr, "exception: %s\n", ex.what());
    return 1;
}
