// Figure 1 / Figure 5-6 anchors with an independent truth-table oracle.
//
// Paper gate -> AIG: g1..g9 raw literals are 14,16,18,20,23,25,26,29,30 (see fixtures/paper/fig1.aag).
// PIs a..f are AIG vars 1..6.
#include "satrar/aig/AagParser.hpp"
#include "satrar/analysis/FaultContext.hpp"
#include "satrar/sat/CnfEncoder.hpp"

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>

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

const std::map<std::string, Literal>& paper() {
    static const std::map<std::string, Literal> map = {
        {"a", Literal(2)},   {"b", Literal(4)},   {"c", Literal(6)},   {"d", Literal(8)},
        {"e", Literal(10)},  {"f", Literal(12)},  {"g1", Literal(14)}, {"g2", Literal(16)},
        {"g3", Literal(18)}, {"g4", Literal(20)}, {"g5", Literal(23)}, {"g6", Literal(25)},
        {"g7", Literal(26)}, {"g8", Literal(29)}, {"g9", Literal(30)},
    };
    return map;
}

bool eval(const aig::Circuit& c, const std::vector<aig::NodeId>& order, unsigned mask, Literal lit) {
    std::map<std::uint32_t, bool> value;
    for (std::size_t i = 0; i < c.primaryInputIds().size(); ++i) {
        value[c.nodes()[c.primaryInputIds()[i]].aagVar] = ((mask >> i) & 1u) != 0;
    }
    const auto read = [&](Literal l) {
        if (l.isConstant()) return l.valueIfConstant();
        return value.at(l.variable()) != l.inverted();
    };
    for (const auto id : order) {
        const auto& n = c.nodes()[id];
        if (n.isAnd()) value[n.aagVar] = read(n.andInputs().left) && read(n.andInputs().right);
    }
    return read(lit);
}

}  // namespace

int main() {
#ifndef SATRAR_HAS_MINISAT
    std::fprintf(stderr, "test_paper_anchors: skipped (no MiniSat)\n");
    return 0;
#else
    const auto path = std::filesystem::path(__FILE__).parent_path() / "fixtures/paper/fig1.aag";
    const auto circuit = aig::AagParser::parseFile(path.string());
    const auto order = circuit.topologicalOrder();
    const auto g7 = circuit.findNodeByAigVar(13);
    const auto g9 = circuit.findNodeByAigVar(15);

    // Raw mapping from the spec.
    CHECK(circuit.primaryOutputs().size() == 2);
    CHECK(circuit.primaryOutputs()[0].raw() == 23 && circuit.primaryOutputs()[1].raw() == 30);

    // --- Truth-table oracle: detecting-vector counts 3, 3, 7 and g9 sa0 with g5=0 -> 0.
    const auto detects = [&](const aig::Circuit& bad, const auto& restrict) {
        const auto ob = bad.topologicalOrder();
        unsigned count = 0;
        for (unsigned mask = 0; mask < 64; ++mask) {
            if (!restrict(mask)) continue;
            bool differs = false;
            for (std::size_t po = 0; po < 2; ++po) {
                differs |= eval(circuit, order, mask, circuit.primaryOutputs()[po]) !=
                           eval(bad, ob, mask, bad.primaryOutputs()[po]);
            }
            count += differs;
        }
        return count;
    };
    const auto all = [](unsigned) { return true; };
    auto targetBad = circuit.clone();
    targetBad.replaceAndFanin(g7, 0, Literal(1));  // g6 -> g7 pin 0 stuck-at-1
    auto g7Bad = circuit.clone();
    g7Bad.substituteAigVar(13, Literal(0));
    auto g9Bad = circuit.clone();
    g9Bad.substituteAigVar(15, Literal(0));
    CHECK(detects(targetBad, all) == 3);
    CHECK(detects(g7Bad, all) == 3);
    CHECK(detects(g9Bad, all) == 7);
    CHECK(detects(g9Bad, [&](unsigned mask) { return !eval(circuit, order, mask, paper().at("g5")); }) == 0);

    analysis::FaultContext context(circuit.clone());
    const auto lit = [&](const char* name) {
        const auto edge = paper().at(name);
        return sat::CnfEncoder::dimacsLitForAig(context.cnf(), edge).value();
    };
    const auto value = [&](const char* name) { return context.propagator().value(lit(name)); };

    // --- Target MA (Example 4 / Fig. 1): g6=0,g2=0,d=0,g1=0,g4=0,g5=0,g3=1,f=1,a=1,b=1.
    context.setPinFault({{g7, 0}, analysis::StuckAt::One});
    auto ma = context.computeMandatoryAssignments();
    CHECK(context.status() != analysis::FaultStatus::Conflict);
    const std::pair<const char*, bool> targetExpected[] = {
        {"g6", false}, {"g2", false}, {"d", false}, {"g1", false}, {"g4", false},
        {"g5", false}, {"g3", true},  {"f", true},  {"a", true},   {"b", true}};
    for (const auto& [name, expected] : targetExpected) {
        if (ma.valueOf(lit(name)) != expected) {
            std::fprintf(stderr, "target MA mismatch for %s\n", name);
            return 1;
        }
    }

    // --- Fig. 6 shared prefix: f=1, g4=0 held; switch to g7 output sa0.
    // Decisions are context roots: f=1 then g4=0 (raw literal of g4 is the positive AIG var 10).
    context.setDominatorOutputFault({g7, analysis::StuckAt::Zero});
    const std::vector<sat::DimacsLiteral> prefix = {lit("f"), -lit("g4")};
    ma = context.computeMandatoryAssignments(prefix);
    CHECK(context.status() != analysis::FaultStatus::Conflict);
    // Prefix entries are roots (no reason); the rest carry a gate reason.
    CHECK(context.propagator().reason(lit("f")).empty());
    CHECK(context.propagator().reason(-lit("g4")).empty());
    const auto fLevel = context.propagator().level(lit("f"));
    const auto g4Level = context.propagator().level(-lit("g4"));
    CHECK(fLevel < g4Level);
    const std::pair<const char*, bool> g7Expected[] = {
        {"g7", true}, {"g6", true}, {"g3", true}, {"a", true}, {"b", true}};
    for (const auto& [name, expected] : g7Expected) {
        if (value(name) != expected) {
            std::fprintf(stderr, "g7 context mismatch for %s\n", name);
            return 1;
        }
    }
    CHECK(!context.propagator().reason(lit("g7")).empty() || context.propagator().level(lit("g7")) >= 1);

    // Switching to a different dominator with the same prefix keeps the prefix levels.
    context.setDominatorOutputFault({g9, analysis::StuckAt::Zero});
    (void)context.computeMandatoryAssignments(prefix);
    CHECK(context.propagator().level(lit("f")) == fLevel);

    // --- g7 context, trial d=0: no conflict, g2=1,g5=1,e=1,g1=0,c=0; rollback removes them.
    context.setDominatorOutputFault({g7, analysis::StuckAt::Zero});
    auto trial = prefix;
    trial.push_back(-lit("d"));
    ma = context.computeMandatoryAssignments(trial);
    CHECK(context.status() != analysis::FaultStatus::Conflict);
    const std::pair<const char*, bool> trialExpected[] = {
        {"g2", true}, {"g5", true}, {"e", true}, {"g1", false}, {"c", false}};
    for (const auto& [name, expected] : trialExpected) {
        if (value(name) != expected) {
            std::fprintf(stderr, "trial d=0 mismatch for %s\n", name);
            return 1;
        }
    }
    (void)context.computeMandatoryAssignments(prefix);
    CHECK(!value("g2").has_value());
    CHECK(!value("e").has_value());
    CHECK(value("g7") == true);  // base context kept

    // --- g9 context, trial g5=0 conflicts; g9=1, g8=1, f=1 are the requirements.
    context.setDominatorOutputFault({g9, analysis::StuckAt::Zero});
    (void)context.computeMandatoryAssignments();
    CHECK(value("g9") == true && value("g8") == true && value("f") == true);
    (void)context.computeMandatoryAssignments({-lit("g5")});
    CHECK(context.status() == analysis::FaultStatus::Conflict);
    context.clearFault();
    std::fprintf(stderr, "test_paper_anchors: ok\n");
    return 0;
#endif
}
