#include "satrar/aig/AagParser.hpp"
#include "satrar/cec/AbcCec.hpp"
#include "satrar/rar/EquivalenceChecker.hpp"

#include <cstdio>
#include <filesystem>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d (%s)\n", __FILE__, __LINE__, #cond);            \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

std::filesystem::path labRoot() {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

int main() {
    satrar::cec::AbcCec checker;
    if (!checker.available()) {
        std::fprintf(stderr, "test_abc_cec: SKIP (%s)\n", checker.describe().c_str());
        return 77;
    }
    const satrar::rar::EquivalenceChecker equivalence(checker);
    const auto fig1 = satrar::aig::AagParser::parseFile(
        (labRoot() / "satrar-cpp/tests/fixtures/paper/fig1.aag").string());
    const auto rewritten = satrar::aig::AagParser::parseFile(
        (labRoot() / "satrar-circuit-v2/results/fig1-rewritten.aag").string());
    const auto same = equivalence.check(fig1, rewritten, 60);
    CHECK(same.verdict == satrar::rar::EquivalenceVerdict::Equivalent);

    auto broken = fig1.clone();
    const auto output = broken.primaryOutputs().front();
    // Flip the first output by replacing the gate that drives it when it is an AND.
    // A direct output inversion is expressed by rewriting that AND's function.
    broken = fig1.clone();
    const auto driver = fig1.primaryOutputs().front();
    CHECK(!driver.isConstant());
    const auto id = broken.findNodeByAigVar(driver.variable());
    CHECK(broken.nodes()[id].isAnd());
    broken.replaceAndFanin(id, 0, satrar::aig::Literal(0));
    const auto different = equivalence.check(fig1, broken, 60);
    CHECK(different.verdict == satrar::rar::EquivalenceVerdict::NotEquivalent);
    CHECK(different.counterexampleReplayOk);
    (void)output;
    std::fprintf(stderr, "test_abc_cec: ok\n");
    return 0;
}
