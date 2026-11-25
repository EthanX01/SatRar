#include "satrar/aig/AagParser.hpp"
#include "satrar/aig/WireRef.hpp"
#include "satrar/analysis/Fault.hpp"
#include "satrar/analysis/FaultContext.hpp"
#include "satrar/sat/CnfEncoder.hpp"
#include "satrar/sat/MinisatPropagationEngine.hpp"
#include "satrar/sat/MinisatVerificationSolver.hpp"

#include <filesystem>
#include <iostream>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << " (" #cond ")\n";         \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

int main() {
#ifndef SATRAR_HAS_MINISAT
    std::cout << "test_fault: skipped (no MiniSat)\n";
    return 0;
#else
    CHECK(satrar::sat::MinisatPropagationEngine::runSmokeSelfTest());

    const auto path = std::filesystem::path(__FILE__).parent_path() / "fixtures" / "and.aag";
    auto circuit = satrar::aig::AagParser::parseFile(path.string());
    const satrar::aig::NodeId andId = circuit.findNodeByAigVar(3);
    CHECK(andId != satrar::aig::kInvalidNodeId);

    satrar::analysis::FaultContext ctx(std::move(circuit));
    satrar::analysis::PinFault fault{satrar::aig::WireRef{andId, 0}, satrar::analysis::StuckAt::Zero};
    ctx.setPinFault(fault);

    const auto stuck = ctx.stuckAtAssumption();
    CHECK(stuck.has_value());

    const auto ma = ctx.computeMandatoryAssignments();
    CHECK(ma.size() >= 1);
    CHECK(ma.valueOf(*stuck).has_value());
    CHECK(ma.valueOf(*satrar::sat::CnfEncoder::positiveLit(ctx.cnf(), 1)) == true);
    CHECK(ma.valueOf(*satrar::sat::CnfEncoder::positiveLit(ctx.cnf(), 2)) == true);
    ctx.setPinFault({{andId, 0}, satrar::analysis::StuckAt::One});
    const auto reverse = ctx.computeMandatoryAssignments();
    CHECK(reverse.valueOf(*satrar::sat::CnfEncoder::positiveLit(ctx.cnf(), 1)) == false);
    CHECK(reverse.valueOf(*satrar::sat::CnfEncoder::positiveLit(ctx.cnf(), 2)) == true);

    for (bool detectable : {false, true}) {
        auto reconvergence = satrar::aig::AagParser::parseString(
            detectable ? "aag 3 1 0 1 2\n2\n6\n4 2 1\n6 4 4\n"
                       : "aag 3 1 0 1 2\n2\n6\n4 2 1\n6 4 5\n", "reconvergence");
        const auto id = reconvergence.findNodeByAigVar(2);
        satrar::analysis::FaultContext test(std::move(reconvergence));
        test.setPinFault({{id, 0}, satrar::analysis::StuckAt::Zero});
        const auto result = test.computeMandatoryAssignments();
        CHECK(test.status() == (detectable ? satrar::analysis::FaultStatus::NeedsExactTest
                                          : satrar::analysis::FaultStatus::Conflict));
        using satrar::sat::SatVerdict;
        CHECK(satrar::sat::MinisatVerificationSolver::solve(test.faultDetectionFormula()).verdict ==
              (detectable ? SatVerdict::Sat : SatVerdict::Unsat));
        CHECK((result.size() > 0) == detectable);
    }

    // The first PO is unrelated; only the second observes the reconvergence.
    auto multi = satrar::aig::AagParser::parseString(
        "aag 4 2 0 2 2\n2\n4\n5\n9\n6 2 1\n8 6 6\n", "two outputs");
    const auto start = multi.findNodeByAigVar(3);
    const auto merge = multi.findNodeByAigVar(4);
    satrar::analysis::FaultContext observed(std::move(multi));
    for (const auto kind : {false, true}) {
        if (kind) {
            observed.setDominatorOutputFault({start, satrar::analysis::StuckAt::Zero});
        } else {
            observed.setPinFault({{start, 0}, satrar::analysis::StuckAt::Zero});
        }
        auto projected = observed.computeMandatoryAssignments();
        CHECK(observed.status() == satrar::analysis::FaultStatus::NeedsExactTest);
        CHECK(projected.valueOf(*satrar::sat::CnfEncoder::positiveLit(observed.cnf(), 2)) == std::nullopt);
        CHECK(projected.valueOf(*satrar::sat::CnfEncoder::positiveLit(observed.cnf(), 1)) == true);
        CHECK(merge != satrar::aig::kInvalidNodeId);
        for (const auto& assignment : projected.items()) {
            using satrar::sat::MinisatVerificationSolver;
            using satrar::sat::SatVerdict;
            CHECK(MinisatVerificationSolver::solve(
                observed.faultDetectionFormula({-assignment.lit})).verdict == SatVerdict::Unsat);
        }
    }

    std::cout << "test_fault: ok\n";
    return 0;
#endif
}
