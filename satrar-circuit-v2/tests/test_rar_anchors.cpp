#include "satrar/aig/AagParser.hpp"
#include "satrar/analysis/FaultContext.hpp"
#include "satrar/rar/Rewriter.hpp"
#include "satrar/rar/SatRarEngine.hpp"

#include <algorithm>
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

bool equivalent(const aig::Circuit& left, const aig::Circuit& right) {
    if (left.primaryInputIds().size() != right.primaryInputIds().size() ||
        left.primaryOutputs().size() != right.primaryOutputs().size() ||
        left.primaryInputIds().size() > 16) {
        return false;
    }
    const auto width = left.primaryInputIds().size();
    for (unsigned mask = 0; mask < (1u << width); ++mask) {
        const auto bits = bitsOf(mask, width);
        if (left.evaluate(bits) != right.evaluate(bits)) {
            return false;
        }
    }
    return true;
}

bool readLit(const aig::Circuit& circuit, unsigned mask, Literal lit) {
    if (lit.isConstant()) {
        return lit.valueIfConstant();
    }
    std::map<std::uint32_t, bool> value;
    const auto bits = bitsOf(mask, circuit.primaryInputIds().size());
    for (std::size_t i = 0; i < bits.size(); ++i) {
        value[circuit.nodes()[circuit.primaryInputIds()[i]].aagVar] = bits[i];
    }
    for (const auto id : circuit.topologicalOrder()) {
        const auto& node = circuit.nodes()[id];
        if (!node.isAnd()) {
            continue;
        }
        const auto eval = [&](Literal edge) {
            return edge.isConstant() ? edge.valueIfConstant() : value.at(edge.variable()) != edge.inverted();
        };
        value[node.aagVar] = eval(node.andInputs().left) && eval(node.andInputs().right);
    }
    return value.at(lit.variable()) != lit.inverted();
}

bool separates(const aig::Circuit& circuit, analysis::PinFault target, const rar::Alternative& alt) {
    auto targetBad = circuit.clone();
    targetBad.replaceAndFanin(target.wire.destination, target.wire.inputPin,
                              Literal(static_cast<std::uint32_t>(target.stuckAt)));
    const auto dom = std::visit([](const auto& item) { return item.attachment.dominator; }, alt);
    auto domBad = circuit.clone();
    domBad.substituteAigVar(circuit.nodes()[dom].aagVar, Literal(0));
    const auto signal = [&](unsigned mask) {
        if (const auto* wire = std::get_if<rar::WireAlternative>(&alt)) {
            return !readLit(circuit, mask, wire->source);
        }
        const auto& gate = std::get<rar::GateAlternative>(alt);
        bool conjunction = true;
        for (const auto lit : gate.conjunction) {
            conjunction = conjunction && readLit(circuit, mask, lit);
        }
        bool disjunction = gate.disjunction.empty();
        for (const auto lit : gate.disjunction) {
            disjunction = disjunction || readLit(circuit, mask, lit);
        }
        return conjunction && disjunction;
    };
    const auto width = circuit.primaryInputIds().size();
    for (unsigned mask = 0; mask < (1u << width); ++mask) {
        const auto bits = bitsOf(mask, width);
        const bool targetDetects = circuit.evaluate(bits) != targetBad.evaluate(bits);
        const bool domDetects = circuit.evaluate(bits) != domBad.evaluate(bits);
        if (targetDetects && !signal(mask)) {
            return false;
        }
        if (domDetects && signal(mask)) {
            return false;
        }
    }
    return true;
}

bool twoStage(const aig::Circuit& circuit, analysis::PinFault target, const rar::Alternative& alt) {
    const auto added = rar::Rewriter::addAlternative(circuit, alt);
    if (!equivalent(circuit, added)) {
        return false;
    }
    return equivalent(added, rar::Rewriter::removeTarget(added, target));
}

std::vector<rar::Alternative> search(const aig::Circuit& circuit, analysis::PinFault target,
                                     rar::SatRarOptions options, rar::SatRarStatistics* stats = nullptr) {
    analysis::FaultContext context(circuit.clone());
    rar::SatRarEngine engine(context);
    engine.setOptions(std::move(options));
    engine.setPinTarget(target);
    auto found = engine.run();
    if (stats) {
        *stats = engine.statistics();
    }
    return found;
}

rar::SatRarOptions mode(rar::SatRarMode value) {
    rar::SatRarOptions options;
    options.mode = value;
    return options;
}

bool hasWire(const aig::Circuit& circuit, const std::vector<rar::Alternative>& found, std::uint32_t domVar,
             std::uint32_t sourceRaw) {
    for (const auto& alt : found) {
        const auto* wire = std::get_if<rar::WireAlternative>(&alt);
        if (!wire) {
            continue;
        }
        if (circuit.nodes()[wire->attachment.dominator].aagVar == domVar && wire->source.raw() == sourceRaw) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    const auto root = std::filesystem::path(__FILE__).parent_path();
    const auto fig1 = aig::AagParser::parseFile((root / "fixtures/paper/fig1.aag").string());
    const analysis::PinFault target{{fig1.findNodeByAigVar(13), 0}, analysis::StuckAt::One};
    const auto l0 = search(fig1, target, mode(rar::SatRarMode::TwoWayRarL0));
    const auto l1 = search(fig1, target, mode(rar::SatRarMode::TwoWayRarL1));
    const auto l2 = search(fig1, target, mode(rar::SatRarMode::TwoWayRarL2));
    CHECK(!hasWire(fig1, l0, 15, 23));
    CHECK(!hasWire(fig1, l1, 15, 23));
    CHECK(hasWire(fig1, l2, 15, 23));

    bool gateExample = false;
    const auto gate = search(fig1, target, mode(rar::SatRarMode::SrarGate));
    for (const auto& alt : gate) {
        const auto* item = std::get_if<rar::GateAlternative>(&alt);
        if (!item || fig1.nodes()[item->attachment.dominator].aagVar != 13) {
            continue;
        }
        std::vector<std::uint32_t> conj;
        for (const auto lit : item->conjunction) {
            conj.push_back(lit.raw());
        }
        std::sort(conj.begin(), conj.end());
        if (conj.size() == 2 && conj[0] == 9 && conj[1] == 22 && item->disjunction.empty()) {
            gateExample = true;
        }
        CHECK(twoStage(fig1, target, alt));
        CHECK(separates(fig1, target, alt));
    }
    CHECK(gateExample);
    for (const auto& alt : l2) {
        CHECK(twoStage(fig1, target, alt));
        CHECK(separates(fig1, target, alt));
    }

    const rar::SatRarMode modes[] = {
        rar::SatRarMode::TwoWayRarL0, rar::SatRarMode::TwoWayRarL1, rar::SatRarMode::TwoWayRarL2,
        rar::SatRarMode::SrarWire, rar::SatRarMode::SrarGate, rar::SatRarMode::SrarSubCircuit};
    for (const auto which : modes) {
        for (const bool learn : {true, false}) {
            for (const bool filter : {true, false}) {
                if (which != rar::SatRarMode::SrarGate && (!learn || !filter)) {
                    continue;
                }
                auto options = mode(which);
                options.enableLearning = learn;
                options.enableImplicationFilter = filter;
                for (const auto& alt : search(fig1, target, options)) {
                    CHECK(twoStage(fig1, target, alt));
                    CHECK(separates(fig1, target, alt));
                }
            }
        }
    }

    const auto wire = search(fig1, target, mode(rar::SatRarMode::SrarWire));
    CHECK(hasWire(fig1, wire, 15, 23));
    for (const auto& alt : wire) {
        CHECK(twoStage(fig1, target, alt));
    }

    rar::SatRarStatistics limited;
    auto tiny = mode(rar::SatRarMode::SrarGate);
    tiny.maxTrials = 1;
    const auto partial = search(fig1, target, tiny, &limited);
    CHECK(limited.limitReached);
    for (const auto& alt : partial) {
        CHECK(twoStage(fig1, target, alt));
    }

    const auto provenance = root / "fixtures/provenance";
    struct Fixture {
        const char* file;
        std::uint32_t var;
        analysis::StuckAt stuck;
        rar::SatRarMode which;
        const char* evidence;
        bool learning;
    };
    const Fixture fixtures[] = {
        {"cor21_v11_p0_sa0.aag", 11, analysis::StuckAt::Zero, rar::SatRarMode::SrarGate, "Corollary-2.1", false},
        {"cor22_v4_p0_sa0.aag", 4, analysis::StuckAt::Zero, rar::SatRarMode::SrarGate, "Corollary-2.2", true},
        {"thm3_v10_p0_sa0.aag", 10, analysis::StuckAt::Zero, rar::SatRarMode::SrarSubCircuit, "Theorem-3", true},
    };
    for (const auto& fixture : fixtures) {
        const auto circuit = aig::AagParser::parseFile((provenance / fixture.file).string());
        const analysis::PinFault pin{{circuit.findNodeByAigVar(fixture.var), 0}, fixture.stuck};
        auto options = mode(fixture.which);
        options.enableLearning = fixture.learning;
        const auto found = search(circuit, pin, options);
        std::size_t matched = 0;
        for (const auto& alt : found) {
            if (!twoStage(circuit, pin, alt) || !separates(circuit, pin, alt)) {
                std::fprintf(stderr, "%s candidate failed oracle (%s)\n", fixture.file,
                             std::visit([](const auto& item) { return item.attachment.evidence.c_str(); }, alt));
                return 1;
            }
            const auto evidence = std::visit([](const auto& item) { return item.attachment.evidence; }, alt);
            if (evidence != fixture.evidence) {
                continue;
            }
            ++matched;
            if (const auto* item = std::get_if<rar::GateAlternative>(&alt)) {
                if (std::string(fixture.evidence) == "Corollary-2.1") {
                    CHECK(item->conjunction.size() == 2 && item->disjunction.empty());
                }
                if (std::string(fixture.evidence) == "Corollary-2.2") {
                    CHECK(item->conjunction.size() >= 3 && item->disjunction.empty());
                }
                if (std::string(fixture.evidence) == "Theorem-3") {
                    CHECK(item->disjunction.size() >= 2 && !item->conjunction.empty());
                }
            }
        }
        if (matched == 0) {
            std::fprintf(stderr, "%s: missing %s\n", fixture.file, fixture.evidence);
            return 1;
        }
    }

    std::fprintf(stderr, "test_rar_anchors: ok\n");
    return 0;
}
