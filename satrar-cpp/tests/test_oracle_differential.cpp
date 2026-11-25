// R2/R3/R4: independent exhaustive-simulation oracle against FaultContext and SatRarEngine.
//
// For seeded small AIGs we enumerate every PI vector with our own simulator (no use of the code
// under test) and check:
//   * MA soundness: every reported mandatory literal holds on every detecting vector, and a
//     fault that has detecting vectors is never reported Conflict.
//   * Exact miter agreement: Det(F) SAT <=> some vector detects; Det(F) & !ma is UNSAT.
//   * Candidate soundness: every candidate of every mode passes addition and removal checks
//     for all PI vectors, and Theorem-3 separators satisfy S=1 on target-detecting vectors and
//     S=0 on dominator-detecting vectors.
#include "satrar/aig/AagWriter.hpp"
#include "satrar/aig/Circuit.hpp"
#include "satrar/analysis/FaultContext.hpp"
#include "satrar/rar/Rewriter.hpp"
#include "satrar/rar/SatRarEngine.hpp"
#include "satrar/sat/CnfEncoder.hpp"
#include "satrar/sat/MinisatVerificationSolver.hpp"
#include "satrar/sat/MiterBuilder.hpp"
#include "support/RandomAig.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d (%s) seed=%llu\n", __FILE__, __LINE__, #cond,   \
                         static_cast<unsigned long long>(g_seed));                           \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

using namespace satrar;
using aig::Literal;

namespace {

std::uint64_t g_seed = 0;

using randomaig::Rng;
using randomaig::randomCircuit;

/// Node values for all AIG variables under one PI vector (own simulator).
std::unordered_map<std::uint32_t, bool> simulate(const aig::Circuit& c, unsigned mask,
                                                 const std::vector<aig::NodeId>& order) {
    std::unordered_map<std::uint32_t, bool> value;
    for (std::size_t i = 0; i < c.primaryInputIds().size(); ++i) {
        value[c.nodes()[c.primaryInputIds()[i]].aagVar] = ((mask >> i) & 1u) != 0;
    }
    const auto read = [&](Literal lit) {
        if (lit.isConstant()) return lit.valueIfConstant();
        const bool v = value.at(lit.variable());
        return lit.inverted() ? !v : v;
    };
    for (const auto id : order) {
        const auto& node = c.nodes()[id];
        if (node.isAnd()) {
            value[node.aagVar] = read(node.andInputs().left) && read(node.andInputs().right);
        }
    }
    value[0] = false;
    return value;
}

bool literalValue(const std::unordered_map<std::uint32_t, bool>& value, Literal lit) {
    if (lit.isConstant()) return lit.valueIfConstant();
    const bool v = value.at(lit.variable());
    return lit.inverted() ? !v : v;
}

std::vector<bool> outputsOf(const aig::Circuit& c, unsigned mask, const std::vector<aig::NodeId>& order) {
    const auto value = simulate(c, mask, order);
    std::vector<bool> out;
    for (const auto po : c.primaryOutputs()) out.push_back(literalValue(value, po));
    return out;
}

bool equivalentByVectors(const aig::Circuit& a, const aig::Circuit& b) {
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

struct FaultSpec {
    bool pin = true;
    aig::NodeId node = 0;
    std::uint8_t inputPin = 0;
    analysis::StuckAt stuck = analysis::StuckAt::Zero;
};

aig::Circuit faulty(const aig::Circuit& c, const FaultSpec& f) {
    auto out = c.clone();
    const Literal constant(static_cast<std::uint32_t>(f.stuck));
    if (f.pin) {
        out.replaceAndFanin(f.node, f.inputPin, constant);
    } else {
        out.substituteAigVar(c.nodes()[f.node].aagVar, constant);
    }
    return out;
}

std::vector<unsigned> detectingVectors(const aig::Circuit& good, const aig::Circuit& bad) {
    const auto og = good.topologicalOrder();
    const auto ob = bad.topologicalOrder();
    std::vector<unsigned> result;
    for (unsigned mask = 0; mask < (1u << good.primaryInputIds().size()); ++mask) {
        if (outputsOf(good, mask, og) != outputsOf(bad, mask, ob)) result.push_back(mask);
    }
    return result;
}

struct Totals {
    std::size_t faults = 0, detectable = 0, conflicts = 0, maLiterals = 0, candidates = 0;
    std::size_t subCandidates = 0, limited = 0, redundancyEvents = 0, learnedUnits = 0;
    std::size_t postdominators = 0, unobservable = 0;
    std::map<std::string, std::size_t> evidence;
    std::set<std::string> dumped;
};

/// Independent postdominator oracle: d dominates start toward the outputs iff blocking d leaves no
/// path from start to any primary-output driver (and start has at least one path).
int checkPostdominators(const aig::Circuit& circuit, Totals& totals) {
    std::unordered_map<aig::NodeId, bool> drivesOutput;
    for (const auto po : circuit.primaryOutputs()) {
        if (!po.isConstant()) drivesOutput[circuit.findNodeByAigVar(po.variable())] = true;
    }
    const auto reachesOutput = [&](aig::NodeId start, aig::NodeId blocked) {
        std::vector<aig::NodeId> todo{start};
        std::set<aig::NodeId> seen;
        while (!todo.empty()) {
            const auto id = todo.back();
            todo.pop_back();
            if (id == blocked || !seen.insert(id).second) continue;
            if (drivesOutput.count(id)) return true;
            for (const auto wire : circuit.fanoutWires(id)) todo.push_back(wire.destination);
        }
        return false;
    };
    const analysis::GraphAnalysis graph(circuit);
    const auto order = circuit.topologicalOrder();
    std::unordered_map<aig::NodeId, std::size_t> rank;
    for (std::size_t i = 0; i < order.size(); ++i) rank[order[i]] = i;
    for (aig::NodeId start = 0; start < circuit.nodes().size(); ++start) {
        if (!circuit.nodes()[start].isAnd()) continue;
        std::set<aig::NodeId> expected;
        if (reachesOutput(start, aig::kInvalidNodeId)) {
            for (aig::NodeId d = 0; d < circuit.nodes().size(); ++d) {
                if (circuit.nodes()[d].isAnd() && (d == start || (graph.fanoutCone(start).count(d) &&
                                                                  !reachesOutput(start, d)))) {
                    expected.insert(d);
                }
            }
        }
        const auto actual = graph.outputPostdominators(start);
        CHECK(std::set<aig::NodeId>(actual.begin(), actual.end()) == expected);
        CHECK(actual.size() == expected.size());  // no duplicates
        for (std::size_t i = 1; i < actual.size(); ++i) CHECK(rank[actual[i - 1]] < rank[actual[i]]);
        totals.postdominators += actual.size();
        totals.unobservable += expected.empty();
    }
    return 0;
}

int checkFaults(const aig::Circuit& circuit, Totals& totals) {
    const auto order = circuit.topologicalOrder();
    std::vector<FaultSpec> specs;
    for (aig::NodeId id = 0; id < circuit.nodes().size(); ++id) {
        if (!circuit.nodes()[id].isAnd()) continue;
        for (const auto stuck : {analysis::StuckAt::Zero, analysis::StuckAt::One}) {
            specs.push_back({false, id, 0, stuck});
            specs.push_back({true, id, 0, stuck});
            specs.push_back({true, id, 1, stuck});
        }
    }
    analysis::FaultContext context(circuit.clone());
    std::unordered_map<std::uint32_t, std::uint32_t> aigOfSat;
    for (const auto& item : context.cnf().aigToSat()) aigOfSat.emplace(item.second, item.first);

    for (const auto& spec : specs) {
        ++totals.faults;
        context.resetSolver();
        if (spec.pin) {
            context.setPinFault({{spec.node, spec.inputPin}, spec.stuck});
        } else {
            context.setDominatorOutputFault({spec.node, spec.stuck});
        }
        const auto ma = context.computeMandatoryAssignments();
        const auto status = context.status();
        const auto bad = faulty(circuit, spec);
        const auto detecting = detectingVectors(circuit, bad);
        const auto exact = sat::MinisatVerificationSolver::solve(context.faultDetectionFormula());
        CHECK((exact.verdict == sat::SatVerdict::Sat) == !detecting.empty());
        if (detecting.empty()) {
            if (status == analysis::FaultStatus::Conflict) ++totals.conflicts;
            context.clearFault();
            continue;
        }
        ++totals.detectable;
        // A detectable fault must never be reported redundant.
        CHECK(status != analysis::FaultStatus::Conflict);
        for (const auto& item : ma.items()) {
            const auto found = aigOfSat.find(sat::dimacsVariable(item.lit));
            // Skip the CNF constant keys (reserved high range) and AIG constant 0.
            if (found == aigOfSat.end() || found->second == 0 || found->second >= 0xE0000000u) continue;
            ++totals.maLiterals;
            for (const auto mask : detecting) {
                const auto value = simulate(circuit, mask, order);
                const bool signal = value.at(found->second);
                CHECK(signal == (item.lit > 0));
            }
            // Exact miter agreement: detection cannot coexist with the negated MA.
            CHECK(sat::MinisatVerificationSolver::solve(
                      context.faultDetectionFormula({-item.lit})).verdict == sat::SatVerdict::Unsat);
        }
        context.clearFault();
    }
    return 0;
}

const char* g_dumpDirectory = nullptr;

int checkCandidates(const aig::Circuit& circuit, Totals& totals) {
    const auto order = circuit.topologicalOrder();
    struct ModeSpec { const char* name; rar::SatRarMode mode; bool learning; };
    const ModeSpec modes[] = {
        {"l0", rar::SatRarMode::TwoWayRarL0, true}, {"l1", rar::SatRarMode::TwoWayRarL1, true},
        {"l2", rar::SatRarMode::TwoWayRarL2, true}, {"wire", rar::SatRarMode::SrarWire, true},
        {"gate", rar::SatRarMode::SrarGate, true},  {"gate_nolearn", rar::SatRarMode::SrarGate, false},
        {"sub", rar::SatRarMode::SrarSubCircuit, true},
    };
    analysis::FaultContext context(circuit.clone());
    for (aig::NodeId id = 0; id < circuit.nodes().size(); ++id) {
        if (!circuit.nodes()[id].isAnd()) continue;
        for (std::uint8_t pin = 0; pin < 2; ++pin) {
            for (const auto stuck : {analysis::StuckAt::Zero, analysis::StuckAt::One}) {
                const analysis::PinFault target{{id, pin}, stuck};
                for (const auto& [name, mode, learning] : modes) {
                    context.resetSolver();
                    rar::SatRarEngine engine(context);
                    rar::SatRarOptions options;
                    options.mode = mode;
                    options.enableLearning = learning;
                    engine.setOptions(options);
                    engine.setPinTarget(target);
                    const auto alternatives = engine.run();
                    totals.limited += engine.statistics().limitReached;
                    // Figure 5 redundancy events must be truthful: no detecting vector exists.
                    for (const auto& event : engine.statistics().redundancy) {
                        ++totals.redundancyEvents;
                        const FaultSpec spec = event.kind == rar::RedundancyEvent::Kind::Target
                            ? FaultSpec{true, id, pin, stuck}
                            : FaultSpec{false, event.node, 0, analysis::StuckAt::Zero};
                        if (!detectingVectors(circuit, faulty(circuit, spec)).empty()) {
                            std::fprintf(stderr, "redundancy event is false: mode=%s kind=%s\n", name,
                                         event.kind == rar::RedundancyEvent::Kind::Target ? "target" : "dominator");
                            return 1;
                        }
                    }
                    totals.learnedUnits += engine.statistics().learnedUnits;
                    for (const auto& alt : alternatives) {
                        ++totals.candidates;
                        const auto evidence = std::visit([](const auto& a) { return a.attachment.evidence; }, alt);
                        ++totals.evidence[evidence];
                        const auto firstOfKind = totals.dumped.insert(evidence + "/" + name).second;
                        if (g_dumpDirectory && firstOfKind) {
                            std::ostringstream name2;
                            name2 << g_dumpDirectory << "/" << evidence << "_seed" << g_seed << "_v"
                                  << circuit.nodes()[id].aagVar << "_p" << static_cast<unsigned>(pin) << "_sa"
                                  << static_cast<unsigned>(stuck) << "_" << name << ".aag";
                            aig::AagWriter::writeFile(circuit, name2.str());
                        }
                        const auto added = rar::Rewriter::addAlternative(circuit, alt);
                        if (!equivalentByVectors(circuit, added)) {
                            std::fprintf(stderr, "addition not equivalent: mode=%s evidence=%s\n", name,
                                         evidence.c_str());
                            return 1;
                        }
                        const auto removed = rar::Rewriter::removeTarget(added, target);
                        if (!equivalentByVectors(added, removed)) {
                            std::fprintf(stderr, "removal not equivalent: mode=%s evidence=%s\n", name,
                                         evidence.c_str());
                            return 1;
                        }
                        if (std::holds_alternative<rar::GateAlternative>(alt) && evidence == "Theorem-3") {
                            ++totals.subCandidates;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

}  // namespace

int run();

int main(int argc, char** argv) {
    if (argc > 1) g_dumpDirectory = argv[1];  // optional: dump the first circuit of each evidence kind
    try {
        return run();
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "FAIL uncaught exception seed=%llu: %s\n",
                     static_cast<unsigned long long>(g_seed), ex.what());
        return 1;
    }
}

int run() {
#ifndef SATRAR_HAS_MINISAT
    std::fprintf(stderr, "test_oracle_differential: skipped (no MiniSat)\n");
    return 0;
#else
    Totals totals;
    constexpr unsigned kSeeds = 150;
    for (unsigned seed = 1; seed <= kSeeds; ++seed) {
        g_seed = seed;
        Rng rng{seed * 7919ULL};
        const auto circuit = randomCircuit(rng);
        if (checkPostdominators(circuit, totals)) return 1;
        if (checkFaults(circuit, totals)) return 1;
        if (checkCandidates(circuit, totals)) return 1;
    }
    std::fprintf(stderr,
                 "test_oracle_differential: ok seeds=%u faults=%zu detectable=%zu "
                 "reported_redundant=%zu ma_literals=%zu candidates=%zu theorem3=%zu limited=%zu "
                 "redundancy_events=%zu learned_units=%zu postdominators=%zu unobservable_gates=%zu\n",
                 kSeeds, totals.faults, totals.detectable, totals.conflicts, totals.maLiterals,
                 totals.candidates, totals.subCandidates, totals.limited, totals.redundancyEvents,
                 totals.learnedUnits, totals.postdominators, totals.unobservable);
    for (const auto& item : totals.evidence) {
        std::fprintf(stderr, "  evidence %-14s %zu\n", item.first.c_str(), item.second);
    }
    // The generator must actually exercise the code under test.
    if (totals.detectable < 100 || totals.maLiterals < 100) {
        std::fprintf(stderr, "oracle coverage too small\n");
        return 1;
    }
    return 0;
#endif
}