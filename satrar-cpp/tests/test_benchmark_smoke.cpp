#include "satrar/aig/AagParser.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d (%s)\n", __FILE__, __LINE__, #cond);            \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

static bool parseOk(const std::filesystem::path& root, const char* rel) {
    const auto path = root / rel;
    auto c = satrar::aig::AagParser::parseFile(path.string());
    return c.stats().andGates > 0;
}

int main() {
    const auto root =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "benchmarks" / "inputs";

    static const char* kEpfl[] = {
        "epfl-random_control/ctrl.aag",   "epfl-random_control/dec.aag",
        "epfl-arithmetic/adder.aag",      "epfl-arithmetic/sin.aag",
    };

    for (const char* rel : kEpfl) {
        if (!std::filesystem::exists(root / rel)) {
            std::fprintf(stderr, "skip missing %s\n", rel);
            continue;
        }
        try {
            CHECK(parseOk(root, rel));
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "parse failed %s: %s\n", rel, ex.what());
            return 1;
        }
    }

    std::fprintf(stderr, "test_benchmark_smoke: ok\n");
    return 0;
}