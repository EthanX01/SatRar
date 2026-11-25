#include "satrar/aig/AagParser.hpp"
#include "satrar/analysis/FaultContext.hpp"

#include <cstdio>
#include <filesystem>
#include <functional>
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
    static const std::map<std::string, Literal> names = {
        {"a", Literal(2)},   {"b", Literal(4)},   {"c", Literal(6)},   {"d", Literal(8)},
        {"e", Literal(10)},  {"f", Literal(12)},  {"g1", Literal(14)}, {"g2", Literal(16)},
        {"g3", Literal(18)}, {"g4", Literal(20)}, {"g5", Literal(23)}, {"g6", Literal(25)},
        {"g7", Literal(26)}, {"g8", Literal(29)}, {"g9", Literal(30)},
    };
    return names;
}

std::vector<bool> bitsOf(unsigned mask, std::size_t width) {
    std::vector<bool> bits(width);
    for (std::size_t i = 0; i < width; ++i) {
        bits[i] = ((mask >> i) & 1u) != 0;
    }
    return bits;
}

unsigned detecting(const aig::Circuit& good, const aig::Circuit& bad, const std::function<bool(unsigned)>& keep) {
    unsigned count = 0;
    const auto width = good.primaryInputIds().size();
    for (unsigned mask = 0; mask < (1u << width); ++mask) {
        if (!keep(mask)) {
            continue;
        }
        if (good.evaluate(bitsOf(mask, width)) != bad.evaluate(bitsOf(mask, width))) {
            ++count;
        }
    }
    return count;
}

bool holdsOnDetecting(const aig::Circuit& good, const aig::Circuit& bad, Literal lit) {
    const auto width = good.primaryInputIds().size();
    const auto order = good.topologicalOrder();
    for (unsigned mask = 0; mask < (1u << width); ++mask) {
        const auto bits = bitsOf(mask, width);
        if (good.evaluate(bits) == bad.evaluate(bits)) {
            continue;
        }
        std::map<std::uint32_t, bool> value;
        for (std::size_t i = 0; i < width; ++i) {
            value[good.nodes()[good.primaryInputIds()[i]].aagVar] = bits[i];
        }
        for (const auto id : order) {
            const auto& node = good.nodes()[id];
            if (!node.isAnd()) {
                continue;
            }
            const auto read = [&](Literal edge) {
                if (edge.isConstant()) {
                    return edge.valueIfConstant();
                }
                return value.at(edge.variable()) != edge.inverted();
            };
            value[node.aagVar] = read(node.andInputs().left) && read(node.andInputs().right);
        }
        const bool got = lit.isConstant() ? lit.valueIfConstant()
                                          : value.at(lit.variable()) != lit.inverted();
        if (!got) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    const auto path = std::filesystem::path(__FILE__).parent_path() / "fixtures/paper/fig1.aag";
    const auto circuit = aig::AagParser::parseFile(path.string());
    const auto g7 = circuit.findNodeByAigVar(13);
    const auto g9 = circuit.findNodeByAigVar(15);
    CHECK(circuit.primaryOutputs().size() == 2);
    CHECK(circuit.primaryOutputs()[0].raw() == 23);
    CHECK(circuit.primaryOutputs()[1].raw() == 30);

    auto targetBad = circuit.clone();
    targetBad.replaceAndFanin(g7, 0, Literal(1));
    auto g7Bad = circuit.clone();
    g7Bad.substituteAigVar(13, Literal(0));
    auto g9Bad = circuit.clone();
    g9Bad.substituteAigVar(15, Literal(0));
    const auto all = [](unsigned) { return true; };
    CHECK(detecting(circuit, targetBad, all) == 3);
    CHECK(detecting(circuit, g7Bad, all) == 3);
    CHECK(detecting(circuit, g9Bad, all) == 7);

    analysis::FaultContext context(circuit.clone());
    context.setPinFault({{g7, 0}, analysis::StuckAt::One});
    auto ma = context.computeMandatoryAssignments();
    CHECK(context.status() != analysis::FaultStatus::Conflict);
    const std::pair<const char*, bool> targetExpected[] = {
        {"g6", false}, {"g2", false}, {"d", false}, {"g1", false}, {"g4", false},
        {"g5", false}, {"g3", true},  {"f", true},  {"a", true},   {"b", true}};
    for (const auto& [name, expected] : targetExpected) {
        if (ma.valueOf(paper().at(name)) != expected) {
            std::fprintf(stderr, "target MA mismatch %s\n", name);
            return 1;
        }
        const auto lit = expected ? paper().at(name) : ~paper().at(name);
        CHECK(holdsOnDetecting(circuit, targetBad, lit));
    }

    const std::vector<Literal> prefix = {paper().at("f"), ~paper().at("g4")};
    context.setDominatorOutputFault({g7, analysis::StuckAt::Zero});
    ma = context.computeMandatoryAssignments(prefix);
    CHECK(context.status() != analysis::FaultStatus::Conflict);
    CHECK(context.session().isDecision(paper().at("f")));
    CHECK(context.session().isDecision(~paper().at("g4")));
    const auto fLevel = context.session().level(paper().at("f"));
    const auto g4Level = context.session().level(~paper().at("g4"));
    CHECK(fLevel < g4Level);
    const std::pair<const char*, bool> g7Expected[] = {
        {"g7", true}, {"g6", true}, {"g3", true}, {"a", true}, {"b", true}};
    for (const auto& [name, expected] : g7Expected) {
        if (context.session().value(paper().at(name)) != expected) {
            std::fprintf(stderr, "g7 context mismatch %s\n", name);
            return 1;
        }
    }

    context.setDominatorOutputFault({g9, analysis::StuckAt::Zero});
    (void)context.computeMandatoryAssignments(prefix);
    CHECK(context.session().level(paper().at("f")) == fLevel);

    context.setDominatorOutputFault({g7, analysis::StuckAt::Zero});
    auto trial = prefix;
    trial.push_back(~paper().at("d"));
    (void)context.computeMandatoryAssignments(trial);
    CHECK(context.status() != analysis::FaultStatus::Conflict);
    const std::pair<const char*, bool> trialExpected[] = {
        {"g2", true}, {"g5", true}, {"e", true}, {"g1", false}, {"c", false}};
    for (const auto& [name, expected] : trialExpected) {
        if (context.session().value(paper().at(name)) != expected) {
            std::fprintf(stderr, "trial d=0 mismatch %s got value-known=%d\n", name,
                         context.session().value(paper().at(name)).has_value());
            return 1;
        }
    }
    (void)context.computeMandatoryAssignments(prefix);
    CHECK(!context.session().value(paper().at("g2")));
    CHECK(!context.session().value(paper().at("e")));
    CHECK(context.session().value(paper().at("g7")) == true);

    context.setDominatorOutputFault({g9, analysis::StuckAt::Zero});
    (void)context.computeMandatoryAssignments();
    CHECK(context.session().value(paper().at("g9")) == true);
    CHECK(context.session().value(paper().at("g8")) == true);
    CHECK(context.session().value(paper().at("f")) == true);
    (void)context.computeMandatoryAssignments({~paper().at("g5")});
    CHECK(context.status() == analysis::FaultStatus::Conflict);

    context.setPinFault({{g7, 0}, analysis::StuckAt::One});
    ma = context.computeMandatoryAssignments();
    CHECK(ma.valueOf(paper().at("g6")) == false);
    CHECK(ma.valueOf(paper().at("a")) == true);
    context.resetSolver();
    ma = context.computeMandatoryAssignments();
    CHECK(ma.valueOf(paper().at("g6")) == false);

    solver::Budget budget;
    budget.decisionLimit = 1000;
    context.setPinFault({{g7, 0}, analysis::StuckAt::One});
    const auto exact = context.solveDetection({}, budget, false);
    CHECK(exact.state == solver::SolveState::Sat);
    unsigned witnesses = 0;
    for (unsigned mask = 0; mask < 64; ++mask) {
        witnesses += detecting(circuit, targetBad, [&](unsigned bit) { return bit == mask; });
    }
    CHECK(witnesses == 3);

    std::fprintf(stderr, "test_fault_ma: ok\n");
    return 0;
}
