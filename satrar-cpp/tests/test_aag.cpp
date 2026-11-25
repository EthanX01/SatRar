#include "satrar/aig/AagParser.hpp"
#include "satrar/aig/Circuit.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <tuple>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << " (" #cond ")\n";         \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

static std::filesystem::path fixture(const char* name) {
    return std::filesystem::path(__FILE__).parent_path() / "fixtures" / name;
}

static bool evalHalfAdder(bool a, bool b, bool& sum, bool& carry) {
    const auto circuit = satrar::aig::AagParser::parseFile(fixture("halfadder.aag").string());
    const std::vector<bool> outs = circuit.evaluate({a, b});
    if (outs.size() != 2) {
        return false;
    }
    sum = outs[0];
    carry = outs[1];
    return true;
}

int main() {
    using satrar::aig::AagParser;
    using satrar::aig::CircuitStats;

    {
        const auto circuit = AagParser::parseFile(fixture("and.aag").string());
        CircuitStats st = circuit.stats();
        CHECK(st.primaryInputs == 2);
        CHECK(st.primaryOutputs == 1);
        CHECK(st.andGates == 1);
        CHECK(circuit.evaluate({false, false}) == std::vector<bool>{false});
        CHECK(circuit.evaluate({true, true}) == std::vector<bool>{true});
        CHECK(circuit.evaluate({true, false}) == std::vector<bool>{false});
    }

    for (const auto& row : std::vector<std::tuple<bool, bool, bool, bool>>{
             {false, false, false, false},
             {false, true, true, false},
             {true, false, true, false},
             {true, true, false, true},
         }) {
        bool s = false, c = false;
        CHECK(evalHalfAdder(std::get<0>(row), std::get<1>(row), s, c));
        CHECK(s == std::get<2>(row));
        CHECK(c == std::get<3>(row));
    }

    bool threw = false;
    try {
        (void)AagParser::parseString("aag 1 0 1 0 0\n", "latch_test");
    } catch (const std::exception& ex) {
        threw = std::string(ex.what()).find("latches") != std::string::npos;
    }
    CHECK(threw);

    std::cout << "test_aag: ok\n";
    return 0;
}