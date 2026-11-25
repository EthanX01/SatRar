#include "satrar/aig/AagParser.hpp"
#include "satrar/aig/Literal.hpp"
#include "satrar/analysis/Fault.hpp"
#include "satrar/analysis/FaultContext.hpp"
#include "satrar/rar/CircuitRewriter.hpp"
#include "satrar/rar/EquivalenceChecker.hpp"
#include "satrar/rar/RarVerifier.hpp"
#include "satrar/rar/SatRarEngine.hpp"
#include "satrar/sat/CadicalBackend.hpp"
#include "satrar/sat/MiterBuilder.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d (%s)\n", __FILE__, __LINE__, #cond);            \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

int main() {
    const auto fx = std::filesystem::path(__FILE__).parent_path() / "fixtures";

    {
        const auto golden = satrar::aig::AagParser::parseFile((fx / "and.aag").string());
        const auto half = satrar::aig::AagParser::parseFile((fx / "halfadder.aag").string());
        const satrar::sat::CnfFormula miter =
            satrar::sat::MiterBuilder::buildNotEquivalentFormula(golden, golden);
        CHECK(miter.numVariables() > 0);
        CHECK(miter.clauses().size() > golden.stats().andGates);
        (void)half;
    }

    satrar::sat::CadicalBackend backend;
    if (backend.executable().empty()) {
        std::fprintf(stderr, "test_equivalence: SKIP (cadical unavailable)\n");
        return 77;
    }

    CHECK(backend.available());
    const satrar::rar::EquivalenceChecker checker(backend);

    {
        const auto golden = satrar::aig::AagParser::parseFile((fx / "and.aag").string());
        const auto r = checker.check(golden, golden, 60u);
        CHECK(r.verdict == satrar::rar::EquivalenceVerdict::Equivalent);
    }

    {
        const auto golden = satrar::aig::AagParser::parseFile((fx / "halfadder.aag").string());
        const auto r = checker.check(golden, golden, 60u);
        CHECK(r.verdict == satrar::rar::EquivalenceVerdict::Equivalent);
    }

    {
        auto golden = satrar::aig::AagParser::parseFile((fx / "and.aag").string());
        auto broken = golden.clone();
        const satrar::aig::NodeId andId = broken.findNodeByAigVar(3);
        CHECK(andId != satrar::aig::kInvalidNodeId);
        broken.replaceAndFanin(andId, 0, satrar::aig::Literal(satrar::aig::Literal::kConstFalse));
        const auto r = checker.check(golden, broken, 60u);
        CHECK(r.verdict == satrar::rar::EquivalenceVerdict::NotEquivalent);
        CHECK(r.counterexampleReplayOk);
        auto parallel = std::async(std::launch::async, [&] { return checker.check(golden, golden, 60u); });
        CHECK(checker.check(golden, broken, 60u).verdict == satrar::rar::EquivalenceVerdict::NotEquivalent);
        CHECK(parallel.get().verdict == satrar::rar::EquivalenceVerdict::Equivalent);
    }

#if defined(SATRAR_HAS_MINISAT) && SATRAR_HAS_MINISAT
    {
        const auto golden = satrar::aig::AagParser::parseFile((fx / "and_self.aag").string());
        const satrar::aig::NodeId andId = golden.findNodeByAigVar(2);
        CHECK(andId != satrar::aig::kInvalidNodeId);

        satrar::analysis::PinFault fault{{andId, 0}, satrar::analysis::StuckAt::One};
        const satrar::rar::WireAlternative w{{fault, andId, "fixture"}, satrar::aig::Literal(2)};
        const auto patched = satrar::rar::CircuitRewriter::applyWireAlternative(golden, w);
        const auto r = checker.check(golden, patched, 60u);
        CHECK(r.verdict == satrar::rar::EquivalenceVerdict::Equivalent);
        const satrar::rar::RarVerifier verifier(backend);
        CHECK(verifier.verifyWireAlternative(golden, w).acceptance == satrar::rar::RarAcceptance::Accepted);
        auto wrongRemoval = w;
        wrongRemoval.attachment.target.stuckAt = satrar::analysis::StuckAt::Zero;
        const auto bad = verifier.verifyWireAlternative(golden, wrongRemoval);
        CHECK(bad.addition.verdict == satrar::rar::EquivalenceVerdict::Equivalent);
        CHECK(bad.removal && bad.removal->verdict == satrar::rar::EquivalenceVerdict::NotEquivalent);
        CHECK(bad.acceptance == satrar::rar::RarAcceptance::Rejected);
    }
#endif

    std::fprintf(stderr, "test_equivalence: ok\n");
    return 0;
}
