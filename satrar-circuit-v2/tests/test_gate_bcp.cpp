#include "satrar/aig/Circuit.hpp"
#include "satrar/solver/GateNet.hpp"
#include "satrar/solver/SolverSession.hpp"

#include <cstdio>
#include <optional>
#include <vector>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d (%s)\n", __FILE__, __LINE__, #cond);            \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

using satrar::aig::Circuit;
using satrar::aig::Literal;
using satrar::solver::Budget;
using satrar::solver::GateNet;
using satrar::solver::QueryState;
using satrar::solver::SolverSession;

namespace {

Circuit andGate(Literal left, Literal right) {
    Circuit circuit;
    circuit.addConstantNode(false);
    circuit.addConstantNode(true);
    if (!left.isConstant()) {
        circuit.addPrimaryInput(left.variable());
    }
    if (!right.isConstant() && (left.isConstant() || right.variable() != left.variable())) {
        circuit.addPrimaryInput(right.variable());
    }
    circuit.addAndGate(3, left, right);
    circuit.addPrimaryOutput(Literal::fromVariable(3));
    circuit.validate();
    return circuit;
}

int closureMatchesTruth(Literal left, Literal right) {
    const auto circuit = andGate(left, right);
    const Literal z = Literal::fromVariable(3);
    const Literal signals[] = {left, right, z};
    for (int xi = 0; xi < 3; ++xi) {
        for (int yi = 0; yi < 3; ++yi) {
            for (int zi = 0; zi < 3; ++zi) {
                const int assign[3] = {xi, yi, zi};
                std::optional<bool> partial[3];
                for (int i = 0; i < 3; ++i) {
                    if (assign[i] != 2) {
                        partial[i] = assign[i] == 1;
                    }
                }
                bool any = false;
                std::optional<bool> forced[3];
                bool seen[3] = {};
                for (int mask = 0; mask < 4; ++mask) {
                    const bool sameVar = !left.isConstant() && !right.isConstant() &&
                                         left.variable() == right.variable();
                    const bool xv = left.isConstant() ? false : ((mask & 1) != 0);
                    const bool yv = right.isConstant() ? false : (sameVar ? xv : ((mask & 2) != 0));
                    const bool xLogic = left.isConstant() ? left.valueIfConstant() : (left.inverted() ? !xv : xv);
                    const bool yLogic = right.isConstant() ? right.valueIfConstant() : (right.inverted() ? !yv : yv);
                    if (partial[0] && *partial[0] != xLogic) {
                        continue;
                    }
                    if (partial[1] && *partial[1] != yLogic) {
                        continue;
                    }
                    const bool zv = xLogic && yLogic;
                    if (partial[2] && *partial[2] != zv) {
                        continue;
                    }
                    const bool model[3] = {xLogic, yLogic, zv};
                    if (!any) {
                        for (int i = 0; i < 3; ++i) {
                            forced[i] = model[i];
                        }
                        any = true;
                    } else {
                        for (int i = 0; i < 3; ++i) {
                            if (forced[i] && *forced[i] != model[i]) {
                                forced[i].reset();
                            }
                        }
                    }
                    (void)seen;
                }
                bool constantConflict = false;
                for (int i = 0; i < 3; ++i) {
                    if (signals[i].isConstant() && partial[i] &&
                        *partial[i] != signals[i].valueIfConstant()) {
                        constantConflict = true;
                    }
                }
                if (constantConflict) {
                    CHECK(!any);
                    continue;
                }
                GateNet net(circuit);
                SolverSession session(net);
                std::vector<Literal> assumptions;
                for (int i = 0; i < 3; ++i) {
                    if (!partial[i] || signals[i].isConstant()) {
                        continue;
                    }
                    assumptions.push_back(*partial[i] ? signals[i] : ~signals[i]);
                }
                const auto state = session.setAssumptions(assumptions);
                if (!any) {
                    if (state != QueryState::Conflict) {
                        std::fprintf(stderr, "expected conflict x=%d y=%d z=%d\n", xi, yi, zi);
                        return 1;
                    }
                    continue;
                }
                if (state == QueryState::Conflict) {
                    std::fprintf(stderr, "unexpected conflict x=%d y=%d z=%d\n", xi, yi, zi);
                    return 1;
                }
                for (int i = 0; i < 3; ++i) {
                    if (signals[i].isConstant()) {
                        continue;
                    }
                    const auto got = session.value(signals[i]);
                    if (forced[i] && got != *forced[i]) {
                        std::fprintf(stderr, "closure mismatch i=%d x=%d y=%d z=%d\n", i, xi, yi, zi);
                        return 1;
                    }
                    if (!forced[i] && got) {
                        std::fprintf(stderr, "over-forced i=%d x=%d y=%d z=%d\n", i, xi, yi, zi);
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

bool unsatReason(const SolverSession& session, Literal implied) {
    const auto clause = session.reasonClause(implied);
    if (clause.empty()) {
        return true;
    }
    // Premises (negations of the other lits) plus the negated conclusion have no local model:
    // every non-conclusion literal is false under the stored clause, so the clause is the
    // implication. Checking the clause contains the implied literal is the structural half.
    bool contains = false;
    for (const auto lit : clause) {
        contains = contains || lit == implied;
    }
    return contains;
}

}  // namespace

int main() {
    const Literal x = Literal::fromVariable(1);
    const Literal y = Literal::fromVariable(2);
    CHECK(closureMatchesTruth(x, y) == 0);
    CHECK(closureMatchesTruth(~x, y) == 0);
    CHECK(closureMatchesTruth(x, ~y) == 0);
    CHECK(closureMatchesTruth(~x, ~y) == 0);
    CHECK(closureMatchesTruth(x, x) == 0);
    CHECK(closureMatchesTruth(x, ~x) == 0);
    CHECK(closureMatchesTruth(x, Literal(1)) == 0);
    CHECK(closureMatchesTruth(x, Literal(0)) == 0);

    {
        GateNet net(andGate(x, ~x));
        SolverSession session(net);
        CHECK(session.value(Literal::fromVariable(3)) == false);
        CHECK(session.stats().rootAssignments >= 1);
        CHECK(unsatReason(session, ~Literal::fromVariable(3)));
    }
    {
        GateNet net(andGate(x, y));
        SolverSession fresh(net);
        CHECK(fresh.setAssumptions({x, y}) == QueryState::Open);
        CHECK(fresh.value(Literal::fromVariable(3)) == true);
        CHECK(!fresh.learnNow({~x, ~y}));
        CHECK(fresh.inconsistent());

        SolverSession replay(net);
        CHECK(replay.learnNow({~x, ~y}));
        CHECK(replay.setAssumptions({x}) == QueryState::Open);
        CHECK(replay.value(y) == false);

        SolverSession rolled(net);
        CHECK(rolled.setAssumptions({x, y}) == QueryState::Open);
        const auto marked = rolled.mark();
        Budget budget;
        const auto probed = rolled.probe(~x, budget);
        CHECK(probed.state == QueryState::Conflict);
        rolled.rollback(marked);
        CHECK(rolled.value(x) == true);
        CHECK(rolled.value(y) == true);
        CHECK(rolled.value(Literal::fromVariable(3)) == true);
    }
    std::fprintf(stderr, "test_gate_bcp: ok\n");
    return 0;
}
