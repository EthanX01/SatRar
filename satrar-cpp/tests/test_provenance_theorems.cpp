// R3/R4: theorem provenance, learned-clause validity, Theorem 3 two-sided proofs and budgets.
// Fixtures are small seeded AIGs (tests/fixtures/provenance) independent of Figure 1.
#include "satrar/aig/AagParser.hpp"
#include "satrar/analysis/FaultContext.hpp"
#include "satrar/rar/Rewriter.hpp"
#include "satrar/rar/SatRarEngine.hpp"
#include "satrar/sat/CnfEncoder.hpp"
#include "satrar/sat/MinisatVerificationSolver.hpp"
#include "support/Oracle.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>

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

std::vector<rar::Alternative> search(const aig::Circuit& circuit, analysis::PinFault target,
                                     rar::SatRarOptions options, rar::SatRarStatistics* stats = nullptr,
                                     analysis::FaultContext* keep = nullptr) {
    analysis::FaultContext local(circuit.clone());
    auto& context = keep ? *keep : local;
    context.resetSolver();
    rar::SatRarEngine engine(context);
    engine.setOptions(options);
    engine.setPinTarget(target);
    auto result = engine.run();
    if (stats) *stats = engine.statistics();
    return result;
}

rar::SatRarOptions mode(rar::SatRarMode m) {
    rar::SatRarOptions options;
    options.mode = m;
    return options;
}

std::string evidenceOf(const rar::Alternative& alt) {
    return std::visit([](const auto& a) { return a.attachment.evidence; }, alt);
}

aig::NodeId dominatorOf(const rar::Alternative& alt) {
    return std::visit([](const auto& a) { return a.attachment.dominator; }, alt);
}

/// Truth-table separation: the enabling signal blocks nothing the dominator fault needs
/// (S == 0 on every dominator-detecting vector) and fires under every target-detecting vector.
bool separates(const aig::Circuit& c, analysis::PinFault target, const rar::Alternative& alt) {
    const auto order = c.topologicalOrder();
    auto targetBad = c.clone();
    targetBad.replaceAndFanin(target.wire.destination, target.wire.inputPin,
                              Literal(static_cast<std::uint32_t>(target.stuckAt)));
    auto domBad = c.clone();
    domBad.substituteAigVar(c.nodes()[dominatorOf(alt)].aagVar, Literal(0));
    const auto signal = [&](unsigned mask) {
        const auto value = oracle::simulate(c, mask, order);
        if (const auto* wire = std::get_if<rar::WireAlternative>(&alt)) {
            // Wire adds `source` as an enabling input: it equals NOT S.
            return !oracle::read(value, wire->source);
        }
        const auto& gate = std::get<rar::GateAlternative>(alt);
        bool conjunction = true;
        for (const auto lit : gate.conjunction) conjunction &= oracle::read(value, lit);
        bool disjunction = gate.disjunction.empty();
        for (const auto lit : gate.disjunction) disjunction |= oracle::read(value, lit);
        return conjunction && disjunction;
    };
    for (const auto mask : oracle::detecting(c, targetBad)) {
        if (!signal(mask)) return false;
    }
    for (const auto mask : oracle::detecting(c, domBad)) {
        if (signal(mask)) return false;
    }
    return true;
}

bool twoStageEquivalent(const aig::Circuit& c, analysis::PinFault target, const rar::Alternative& alt) {
    const auto added = rar::Rewriter::addAlternative(c, alt);
    if (!oracle::equivalent(c, added)) return false;
    return oracle::equivalent(added, rar::Rewriter::removeTarget(added, target));
}

struct Fixture {
    const char* file;
    std::uint32_t var;
    std::uint8_t pin;
    analysis::StuckAt stuck;
    rar::SatRarMode mode;
    const char* evidence;
    bool learning;
};

}  // namespace

int main() {
#ifndef SATRAR_HAS_MINISAT
    std::fprintf(stderr, "test_provenance_theorems: skipped (no MiniSat)\n");
    return 0;
#else
    const auto root = std::filesystem::path(__FILE__).parent_path() / "fixtures" / "provenance";
    const Fixture fixtures[] = {
        {"cor21_v11_p0_sa0.aag", 11, 0, analysis::StuckAt::Zero, rar::SatRarMode::SrarGate, "Corollary-2.1", false},
        {"cor22_v4_p0_sa0.aag", 4, 0, analysis::StuckAt::Zero, rar::SatRarMode::SrarGate, "Corollary-2.2", true},
        {"thm3_v10_p0_sa0.aag", 10, 0, analysis::StuckAt::Zero, rar::SatRarMode::SrarSubCircuit, "Theorem-3", true},
    };

    for (const auto& fixture : fixtures) {
        const auto circuit = aig::AagParser::parseFile((root / fixture.file).string());
        const analysis::PinFault target{{circuit.findNodeByAigVar(fixture.var), fixture.pin}, fixture.stuck};
        auto fixtureOptions = mode(fixture.mode);
        fixtureOptions.enableLearning = fixture.learning;
        const auto alternatives = search(circuit, target, fixtureOptions);
        std::size_t matched = 0;
        for (const auto& alt : alternatives) {
            // Every candidate of every kind: two-stage equivalence and separation by truth table.
            if (!twoStageEquivalent(circuit, target, alt) || !separates(circuit, target, alt)) {
                std::fprintf(stderr, "%s: candidate with evidence %s fails the oracle\n", fixture.file,
                             evidenceOf(alt).c_str());
                return 1;
            }
            if (evidenceOf(alt) != fixture.evidence) continue;
            ++matched;
            if (const auto* gate = std::get_if<rar::GateAlternative>(&alt)) {
                const std::string kind = fixture.evidence;
                // The label must reflect the number of supporting decisions, not the trial depth.
                if (kind == "Corollary-2.1") CHECK(gate->conjunction.size() == 2 && gate->disjunction.empty());
                if (kind == "Corollary-2.2") CHECK(gate->conjunction.size() >= 3 && gate->disjunction.empty());
                if (kind == "Theorem-3") CHECK(gate->disjunction.size() >= 2 && !gate->conjunction.empty());
            }
        }
        if (matched == 0) {
            std::fprintf(stderr, "%s: no %s candidate found\n", fixture.file, fixture.evidence);
            return 1;
        }
    }

    // Labels follow the supporting-root count in every mode over all fixtures.
    for (const auto& fixture : fixtures) {
        const auto circuit = aig::AagParser::parseFile((root / fixture.file).string());
        const analysis::PinFault target{{circuit.findNodeByAigVar(fixture.var), fixture.pin}, fixture.stuck};
        for (const auto m : {rar::SatRarMode::SrarWire, rar::SatRarMode::SrarGate, rar::SatRarMode::SrarSubCircuit}) {
            for (const auto& alt : search(circuit, target, mode(m))) {
                const auto evidence = evidenceOf(alt);
                const auto* gate = std::get_if<rar::GateAlternative>(&alt);
                if (evidence == "Theorem-1") CHECK(!gate || (gate->conjunction.size() == 1 && gate->disjunction.empty()));
                if (evidence == "Corollary-2.1") CHECK(gate && gate->conjunction.size() == 2 && gate->disjunction.empty());
                if (evidence == "Theorem-2") CHECK(gate && gate->conjunction.size() == 2 && gate->disjunction.empty());
                if (evidence == "Corollary-2.2") CHECK(gate && gate->conjunction.size() >= 3 && gate->disjunction.empty());
                if (evidence == "Generalized-2.1") CHECK(gate && gate->conjunction.size() >= 3 && gate->disjunction.empty());
                if (evidence == "Theorem-3") CHECK(gate && gate->disjunction.size() >= 2);
            }
        }
    }

    // Theorem 3: reject an OR operand whose negation the dominator context does not derive.
    {
        const auto circuit = aig::AagParser::parseFile((root / "thm3_v10_p0_sa0.aag").string());
        const analysis::PinFault target{{circuit.findNodeByAigVar(10), 0}, analysis::StuckAt::Zero};
        const auto alternatives = search(circuit, target, mode(rar::SatRarMode::SrarSubCircuit));
        const auto order = circuit.topologicalOrder();
        std::size_t rejected = 0;
        for (const auto& alt : alternatives) {
            const auto* gate = std::get_if<rar::GateAlternative>(&alt);
            if (!gate || evidenceOf(alt) != "Theorem-3") continue;
            auto domBad = circuit.clone();
            domBad.substituteAigVar(circuit.nodes()[dominatorOf(alt)].aagVar, Literal(0));
            const auto cone = analysis::GraphAnalysis(circuit).fanoutCone(target.wire.destination);
            for (aig::NodeId id = 0; id < circuit.nodes().size(); ++id) {
                if (circuit.nodes()[id].isConstant() || cone.count(id)) continue;
                for (const unsigned inverted : {0u, 1u}) {
                    const Literal extra(2 * circuit.nodes()[id].aagVar + inverted);
                    // Does some dominator-detecting vector (with the conjunction true) make it 1?
                    bool breaks = false;
                    for (const auto mask : oracle::detecting(circuit, domBad)) {
                        const auto value = oracle::simulate(circuit, mask, order);
                        bool conjunction = true;
                        for (const auto lit : gate->conjunction) conjunction &= oracle::read(value, lit);
                        breaks |= conjunction && oracle::read(value, extra);
                    }
                    if (!breaks) continue;
                    auto tampered = *gate;
                    tampered.disjunction.push_back(extra);
                    CHECK(!separates(circuit, target, tampered));
                    try {
                        const auto added = rar::Rewriter::addAlternative(circuit, tampered);
                        CHECK(!oracle::equivalent(circuit, added));  // CEC would reject it too
                        ++rejected;
                    } catch (const std::invalid_argument&) {
                        // Structurally impossible attachment (cycle); also not publishable.
                    }
                }
            }
        }
        CHECK(rejected > 0);
    }

    // Budget: one shared limit across outer trials, splits, probes and deletion retries.
    {
        const auto circuit = aig::AagParser::parseFile((root / "thm3_v10_p0_sa0.aag").string());
        const analysis::PinFault target{{circuit.findNodeByAigVar(10), 0}, analysis::StuckAt::Zero};
        rar::SatRarStatistics full;
        const auto reference = search(circuit, target, mode(rar::SatRarMode::SrarSubCircuit), &full);
        CHECK(!full.limitReached && full.subCandidates >= 1 && full.subProbes >= 2);
        std::set<std::string> steps;
        for (std::size_t budget = 1; budget <= full.trials + 2; ++budget) {
            auto options = mode(rar::SatRarMode::SrarSubCircuit);
            options.maxTrials = budget;
            rar::SatRarStatistics stats;
            const auto limited = search(circuit, target, options, &stats);
            CHECK(stats.trials <= budget);  // never exceeds the reported limit
            if (stats.limitReached) {
                steps.insert(stats.limitStep);
                CHECK(!stats.limitScope.empty());
            }
            // Candidate counts are not monotone in the budget (learning changes later probes);
            // what must hold is that every published candidate is sound.
            for (const auto& alt : limited) {
                CHECK(twoStageEquivalent(circuit, target, alt));  // partial results stay sound
            }
        }
        CHECK(steps.count("trial") + steps.count("sub_probe") + steps.count("sub_shrink") > 0);
        std::fprintf(stderr, "  budget stops observed:");
        for (const auto& step : steps) std::fprintf(stderr, " %s", step.c_str());
        std::fprintf(stderr, " (full run: trials=%zu probes=%zu reductions=%zu)\n", full.trials,
                     full.subProbes, full.subReductions);
    }

    // R3: learned clauses are circuit entailments; gate_nolearn learns nothing yet still emits.
    {
        std::size_t clauses = 0, units = 0, noLearnConflictEmits = 0, learnedConflictEmits_ = 0;
        const char* files[] = {"cor21_v11_p0_sa0.aag", "cor22_v4_p0_sa0.aag", "thm3_v10_p0_sa0.aag"};
        for (const auto file : files) {
            const auto circuit = aig::AagParser::parseFile((root / file).string());
            for (aig::NodeId id = 0; id < circuit.nodes().size(); ++id) {
                if (!circuit.nodes()[id].isAnd()) continue;
                for (std::uint8_t pin = 0; pin < 2; ++pin) {
                    for (const auto stuck : {analysis::StuckAt::Zero, analysis::StuckAt::One}) {
                        const analysis::PinFault target{{id, pin}, stuck};
                        analysis::FaultContext context(circuit.clone());
                        rar::SatRarStatistics learning, cold;
                        const auto withLearning = search(circuit, target, mode(rar::SatRarMode::SrarGate), &learning, &context);
                        for (const auto& clause : learning.learnedClauses) {
                            auto cnf = context.cnf();
                            for (const auto lit : clause) cnf.addClause({-lit});  // negate: all premises hold
                            CHECK(sat::MinisatVerificationSolver::solve(cnf).verdict == sat::SatVerdict::Unsat);
                            ++clauses;
                        }
                        units += learning.learnedUnits;
                        auto options = mode(rar::SatRarMode::SrarGate);
                        options.enableLearning = false;
                        const auto withoutLearning = search(circuit, target, options, &cold);
                        CHECK(cold.learned == 0 && cold.learnedClauses.empty());
                        // Without learning every conflict still emits its candidate.
                        std::size_t conflictEmits = 0, learnedConflictEmits = 0;
                        for (const auto& alt : withoutLearning) {
                            const auto e = evidenceOf(alt);
                            conflictEmits += e == "Theorem-1" || e == "Corollary-2.1" || e == "Generalized-2.1";
                        }
                        for (const auto& alt : withLearning) {
                            const auto e = evidenceOf(alt);
                            learnedConflictEmits += e == "Theorem-1" || e == "Corollary-2.1" || e == "Generalized-2.1";
                        }
                        noLearnConflictEmits += conflictEmits;
                        learnedConflictEmits_ += learnedConflictEmits;
                    }
                }
            }
        }
        CHECK(clauses > 0);
        CHECK(noLearnConflictEmits > 0);       // gate_nolearn still emits conflict-supported candidates
        CHECK(learnedConflictEmits_ > 0);
        std::fprintf(stderr, "  learned clauses verified as circuit entailments: %zu (new base assignments: %zu)\n",
                     clauses, units);
    }

    std::fprintf(stderr, "test_provenance_theorems: ok\n");
    return 0;
#endif
}
