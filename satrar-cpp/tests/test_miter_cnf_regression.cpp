#include "satrar/aig/AagParser.hpp"
#include "satrar/sat/CnfEncoder.hpp"
#include "satrar/sat/MinisatVerificationSolver.hpp"
#include "satrar/sat/MiterBuilder.hpp"

#include <cstdio>
#include <filesystem>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d (%s)\n", __FILE__, __LINE__, #cond);            \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

int main() {
    const auto path = std::filesystem::path(__FILE__).parent_path() / "fixtures" / "miter" /
                      "and_x0.aag";
    const auto circuit = satrar::aig::AagParser::parseFile(path.string());
    satrar::sat::CnfFormula cnf = satrar::sat::CnfEncoder::encodeCircuit(circuit);
    const auto x = satrar::sat::CnfEncoder::positiveLit(cnf, 1);
    const auto z = satrar::sat::CnfEncoder::positiveLit(cnf, 2);
    CHECK(x.has_value() && z.has_value());
    cnf.addClause({*x});
    cnf.addClause({*z});
    CHECK(satrar::sat::MinisatVerificationSolver::solve(cnf).verdict ==
          satrar::sat::SatVerdict::Unsat);
    CHECK(!circuit.evaluate({true})[0]);

    using satrar::aig::AagParser;
    using satrar::sat::SatVerdict;
    const auto verdict = [](const char* first, const char* second) {
        const auto miter = satrar::sat::MiterBuilder::buildNotEquivalentFormula(
            AagParser::parseString(first, "first"), AagParser::parseString(second, "second"));
        return satrar::sat::MinisatVerificationSolver::solve(miter).verdict;
    };
    CHECK(verdict("aag 0 0 0 1 0\n0\n", "aag 0 0 0 1 0\n1\n") == SatVerdict::Sat);
    CHECK(verdict("aag 3 2 0 1 0\n2\n6\n2\n", "aag 3 2 0 1 0\n4\n6\n4\n") == SatVerdict::Unsat);
    CHECK(verdict("aag 3 2 0 1 0\n2\n6\n2\n", "aag 3 2 0 1 0\n6\n2\n2\n") == SatVerdict::Sat);
    CHECK(verdict("aag 1 1 0 2 0\n2\n0\n2\n", "aag 1 1 0 2 0\n2\n0\n3\n") == SatVerdict::Sat);
    CHECK(verdict("aag 2 1 0 1 0\n4\n4\n", "aag 2 1 0 1 1\n2\n4\n4 2 2\n") == SatVerdict::Unsat);
    CHECK(verdict("aag 1 1 0 1 0\n2\n0\n",
                  "aag 2147483647 1 0 1 1\n2\n4294967294\n4294967294 2 3\n") == SatVerdict::Unsat);
    std::fprintf(stderr, "test_miter_cnf_regression: ok\n");
    return 0;
}
