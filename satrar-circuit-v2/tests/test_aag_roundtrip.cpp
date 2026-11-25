#include "satrar/aig/AagParser.hpp"
#include "satrar/aig/AagWriter.hpp"

#include <cstdio>
#include <filesystem>
#include <sstream>
#include <vector>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d (%s)\n", __FILE__, __LINE__, #cond);            \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

int main() {
    const auto path = std::filesystem::path(__FILE__).parent_path() / "fixtures/paper/fig1.aag";
    const auto circuit = satrar::aig::AagParser::parseFile(path.string());
    std::ostringstream text;
    satrar::aig::AagWriter::write(circuit, text);
    const auto again = satrar::aig::AagParser::parseString(text.str(), "roundtrip.aag");
    CHECK(again.stats().primaryInputs == circuit.stats().primaryInputs);
    CHECK(again.stats().andGates == circuit.stats().andGates);
    CHECK(again.primaryOutputs().size() == circuit.primaryOutputs().size());
    CHECK(again.primaryOutputs()[0].raw() == 23);
    CHECK(again.primaryOutputs()[1].raw() == 30);
    const auto bits = std::vector<bool>{true, false, true, false, true, true};
    CHECK(circuit.evaluate(bits) == again.evaluate(bits));
    std::fprintf(stderr, "test_aag_roundtrip: ok\n");
    return 0;
}
