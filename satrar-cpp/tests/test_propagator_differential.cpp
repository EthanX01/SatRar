// R2/R3: CircuitPropagator against a fresh propagator and against its own snapshots.
//
//   * shared prefixes: after any sequence of setAssumptions / nested trials / conflicts the closure
//     equals a freshly built propagator given the same assumptions;
//   * rollback: cancelToContextLevel restores the exact trail, values and conflict state, also after
//     a conflict and for nested trials;
//   * reasons: every implied literal's reason is a real clause (all other literals false);
//   * learning: a clause installed with learnNow gives the same closure as a fresh propagator that
//     had the clause from the start, and a learned unit survives backtracking.
#include "satrar/analysis/CircuitPropagator.hpp"
#include "satrar/sat/CnfEncoder.hpp"
#include "support/RandomAig.hpp"

#include <algorithm>
#include <cstdio>
#include <set>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d (%s) seed=%u step=%u\n", __FILE__, __LINE__,   \
                         #cond, g_seed, g_step);                                             \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

using namespace satrar;
using sat::DimacsLiteral;
using analysis::CircuitPropagator;

namespace {

unsigned g_seed = 0, g_step = 0;

std::vector<DimacsLiteral> sortedTrail(const CircuitPropagator& p) {
    auto trail = p.trail();
    std::sort(trail.begin(), trail.end());
    return trail;
}

/// Every non-root assignment must be justified by a clause whose other literals are false.
bool reasonsAreClauses(const CircuitPropagator& p) {
    for (const auto lit : p.trail()) {
        const auto& why = p.reason(lit);
        if (why.empty()) continue;
        bool contains = false;
        for (const auto other : why) {
            if (other == lit) {
                contains = true;
            } else if (p.value(other) != false) {
                return false;
            }
        }
        if (!contains) return false;
    }
    return true;
}

}  // namespace

int main() {
    std::size_t comparisons = 0, conflicts = 0, learnedChecks = 0, unitReplays = 0;
    for (g_seed = 1; g_seed <= 120; ++g_seed) {
        randomaig::Rng rng{g_seed * 104729ULL};
        const auto circuit = randomaig::randomCircuit(rng);
        CircuitPropagator live(circuit);
        const auto& cnf = live.cnf();
        std::vector<DimacsLiteral> pool;
        for (const auto& node : circuit.nodes()) {
            if (node.aagVar == 0) continue;
            const auto lit = sat::CnfEncoder::positiveLit(cnf, node.aagVar).value();
            pool.push_back(lit);
            pool.push_back(-lit);
        }
        std::vector<std::vector<DimacsLiteral>> learnedClauses;
        std::vector<DimacsLiteral> assumptions;
        const auto fresh = [&](const std::vector<DimacsLiteral>& list) {
            CircuitPropagator f(circuit);
            for (const auto& clause : learnedClauses) f.learn(clause);
            f.setAssumptions(list);
            return f;
        };
        for (g_step = 0; g_step < 40; ++g_step) {
            // Keep a random-length prefix and extend it: this exercises prefix sharing.
            assumptions.resize(rng.below(static_cast<unsigned>(assumptions.size()) + 1));
            const unsigned extend = rng.below(4);
            for (unsigned i = 0; i < extend; ++i) assumptions.push_back(pool[rng.below(static_cast<unsigned>(pool.size()))]);
            live.setAssumptions(assumptions);
            const auto reference = fresh(assumptions);
            ++comparisons;
            CHECK(live.inconsistent() == reference.inconsistent());
            if (!live.inconsistent()) {
                CHECK(sortedTrail(live) == sortedTrail(reference));
                CHECK(reasonsAreClauses(live));
                for (const auto lit : assumptions) CHECK(live.value(lit) == true);
            } else {
                ++conflicts;
            }

            // Nested trials are undone exactly, whether or not they conflict.
            if (!live.inconsistent()) {
                const auto before = live.trail();
                const auto level = live.contextLevel();
                const unsigned depth = 1 + rng.below(3);
                std::vector<DimacsLiteral> trial;
                bool conflict = false;
                std::vector<DimacsLiteral> clause;
                for (unsigned d = 0; d < depth && !conflict; ++d) {
                    live.newContextLevel();
                    const auto lit = pool[rng.below(static_cast<unsigned>(pool.size()))];
                    trial.push_back(lit);
                    if (!live.pushAssumption(lit)) {
                        conflict = true;
                        // Conflict support is a clause over assumption roots only.
                        auto support = live.decisionSupport();
                        for (auto& premise : support) premise = -premise;
                        clause = std::move(support);
                    }
                }
                live.cancelToContextLevel(level);
                CHECK(live.trail() == before);
                CHECK(!live.inconsistent());
                CHECK(live.propagate());
                if (conflict && !clause.empty() && learnedClauses.size() < 6) {
                    // Install immediately at the restored level and compare to a cold start.
                    std::sort(clause.begin(), clause.end());
                    clause.erase(std::unique(clause.begin(), clause.end()), clause.end());
                    // A support clause only uses roots; it must be a circuit entailment.
                    CircuitPropagator proof(circuit);
                    bool refuted = false;
                    for (const auto lit : clause) {
                        if (!proof.pushAssumption(-lit)) { refuted = true; break; }
                    }
                    if (refuted) {
                        const bool consistent = live.learnNow(clause);
                        learnedClauses.push_back(clause);
                        const auto cold = fresh(assumptions);
                        ++learnedChecks;
                        CHECK(consistent == !cold.inconsistent());
                        if (consistent) CHECK(sortedTrail(live) == sortedTrail(cold));
                        if (clause.size() == 1) {
                            // Unit clause: still true after backtracking to the root and re-asserting.
                            live.cancelToContextLevel(0);
                            ++unitReplays;
                            CHECK(live.value(clause.front()) == true);
                            live.setAssumptions(assumptions);
                        }
                    }
                }
            }
        }
    }
    std::fprintf(stderr,
                 "test_propagator_differential: ok comparisons=%zu conflicts=%zu learned=%zu unit_replays=%zu\n",
                 comparisons, conflicts, learnedChecks, unitReplays);
    if (comparisons < 1000 || conflicts == 0 || learnedChecks == 0) {
        std::fprintf(stderr, "propagator differential coverage too small\n");
        return 1;
    }
    return 0;
}
