#include "satrar/cec/AbcLog.hpp"

#include <cstdio>
#include <sstream>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d (%s)\n", __FILE__, __LINE__, #cond);            \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

int main() {
    const auto script = satrar::cec::buildAbcScript({{7, "golden.aig", "cand_7.aig"}}, 30);
    CHECK(script.find("&cec -T 30 golden.aig cand_7.aig") != std::string::npos);
    CHECK(script.find("SATRAR_BEGIN 7") != std::string::npos);

    std::istringstream equivalent(
        "SATRAR_BEGIN 1\n"
        "Networks are equivalent after structural hashing.  Time =     0.00 sec\n"
        "SATRAR_END 1\n");
    const auto ok = satrar::cec::parseAbcLog(equivalent);
    CHECK(!ok.malformed);
    CHECK(ok.jobs.at(1).verdict == satrar::cec::AbcVerdict::Equivalent);

    std::istringstream different(
        "SATRAR_BEGIN 2 \n"
        "Networks are NOT EQUIVALENT. Output 0 trivially differs.\n"
        "INPUT: i0 = 1'h1, i1 = 1'h0, i2 = 1'h1.\n"
        "SATRAR_END 2\n"
        "SATRAR_BEGIN 3\n"
        "Error: Cannot open input file \"missing.aig\".\n"
        "SATRAR_END 3\n"
        "SATRAR_BEGIN 4\n");
    const auto parsed = satrar::cec::parseAbcLog(different);
    CHECK(parsed.malformed);
    CHECK(parsed.jobs.at(2).verdict == satrar::cec::AbcVerdict::NotEquivalent);
    CHECK(parsed.jobs.at(2).inputs.size() == 3);
    CHECK(parsed.jobs.at(2).inputs[0] && *parsed.jobs.at(2).inputs[0]);
    CHECK(parsed.jobs.at(2).inputs[1] && !*parsed.jobs.at(2).inputs[1]);
    CHECK(parsed.jobs.at(3).verdict == satrar::cec::AbcVerdict::Error);
    CHECK(parsed.jobs.at(4).verdict == satrar::cec::AbcVerdict::Missing);
    std::fprintf(stderr, "test_abc_log: ok\n");
    return 0;
}
