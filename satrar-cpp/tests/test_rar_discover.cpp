
#include "satrar/aig/AagParser.hpp"
#include "satrar/aig/AagWriter.hpp"
#include "satrar/analysis/GraphAnalysis.hpp"
#include "satrar/rar/Rewriter.hpp"
#include "satrar/rar/SatRarEngine.hpp"
#include "satrar/sat/CnfEncoder.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <type_traits>

#define CHECK(cond) do { if (!(cond)) { std::cerr << "line " << __LINE__ << ": " << #cond << '\n'; return 1; } } while (0)

using namespace satrar;

int main() {
    const auto path = std::filesystem::path(__FILE__).parent_path() / "fixtures/paper/fig1.aag";
    const auto circuit = aig::AagParser::parseFile(path.string());
    const auto g7 = circuit.findNodeByAigVar(13);
    const auto g8 = circuit.findNodeByAigVar(14);
    const auto g9 = circuit.findNodeByAigVar(15);
    const analysis::PinFault target{{g7, 0}, analysis::StuckAt::One};
    CHECK((analysis::GraphAnalysis(circuit).dominatorsForWire(target.wire) ==
           std::vector<aig::NodeId>{g7, g8, g9}));

    analysis::FaultContext context(circuit.clone());
    context.setPinFault(target);
    const auto ma = context.computeMandatoryAssignments();
    const auto lit = [&](unsigned var) { return sat::CnfEncoder::positiveLit(context.cnf(), var).value(); };
    CHECK(ma.valueOf(lit(6)) == true);
    CHECK(ma.valueOf(lit(4)) == false);
    CHECK(ma.valueOf(lit(11)) == true); // AIG stores the complement of paper g5.
    CHECK(ma.valueOf(lit(12)) == true);
    const auto sharedLevel = context.propagator().level(lit(6));
    context.setDominatorOutputFault({g7, analysis::StuckAt::Zero});
    const auto domMa = context.computeMandatoryAssignments({-lit(4)});
    CHECK(domMa.valueOf(lit(12)) == false);
    CHECK(domMa.valueOf(lit(11)) == false);
    CHECK(context.propagator().level(lit(6)) == sharedLevel);
    CHECK(!context.propagator().reason(lit(11)).empty());

    const auto search = [&](rar::SatRarMode mode) {
        context.resetSolver();
        rar::SatRarEngine engine(context);
        rar::SatRarOptions options;
        options.mode = mode;
        engine.setOptions(options);
        engine.setPinTarget(target);
        return engine.run();
    };
    const auto hasPaperWire = [&](const auto& alts) {
        return std::any_of(alts.begin(), alts.end(), [&](const auto& alt) {
            const auto* wire = std::get_if<rar::WireAlternative>(&alt);
            return wire && wire->attachment.dominator == g9 && wire->source.raw() == 23;
        });
    };
    CHECK(!hasPaperWire(search(rar::SatRarMode::TwoWayRarL0)));
    CHECK(!hasPaperWire(search(rar::SatRarMode::TwoWayRarL1)));
    CHECK(hasPaperWire(search(rar::SatRarMode::TwoWayRarL2)));
    CHECK(hasPaperWire(search(rar::SatRarMode::SrarWire)));

    const auto gates = search(rar::SatRarMode::SrarSubCircuit);
    bool paperGate = false, lectureGate = false, theorem3 = false;
    for (const auto& alt : gates) {
        if (const auto* gate = std::get_if<rar::GateAlternative>(&alt)) {
            paperGate |= gate->attachment.dominator == g7 &&
                         gate->conjunction == std::vector<aig::Literal>{aig::Literal(9), aig::Literal(22)};
            lectureGate |= gate->attachment.dominator == g9 &&
                           gate->conjunction == std::vector<aig::Literal>{aig::Literal(15), aig::Literal(17)};
            theorem3 |= gate->attachment.evidence == "Theorem-3" && gate->disjunction.size() >= 2;
        }
        const auto added = rar::Rewriter::addAlternative(circuit, alt);
        CHECK(added.driverLiteral(target.wire) == circuit.driverLiteral(target.wire));
        const auto removed = rar::Rewriter::removeTarget(added, target);
        std::ostringstream text;
        aig::AagWriter::write(removed, text);
        const auto reloaded = aig::AagParser::parseString(text.str(), "rewritten");
        for (unsigned mask = 0; mask < 64; ++mask) {
            std::vector<bool> input;
            for (unsigned i = 0; i < 6; ++i) input.push_back(((mask >> i) & 1u) != 0);
            CHECK(circuit.evaluate(input) == added.evaluate(input));
            CHECK(added.evaluate(input) == removed.evaluate(input));
            CHECK(removed.evaluate(input) == reloaded.evaluate(input));
        }
    }
    CHECK(paperGate);
    CHECK(lectureGate);
    CHECK(theorem3);
}
