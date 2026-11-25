#include "satrar/aig/AagParser.hpp"
#include "satrar/analysis/CircuitPropagator.hpp"
#include "satrar/sat/CnfEncoder.hpp"

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
    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path();
    const auto circuit = satrar::aig::AagParser::parseFile((root / "tests/fixtures/and.aag").string());
    satrar::analysis::CircuitPropagator prop(circuit);
    CHECK(prop.contextLevel() >= 1);
    const auto pos = satrar::sat::CnfEncoder::positiveLit(prop.cnf(), 2);
    CHECK(pos.has_value());
    CHECK(prop.pushAssumption(*pos));
    std::vector<satrar::sat::DimacsLiteral> tr;
    CHECK(prop.propagate(&tr));
    std::cout << "test_circuit_propagator ok\n";
    return 0;
}