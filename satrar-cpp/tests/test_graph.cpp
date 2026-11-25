#include "satrar/aig/AagParser.hpp"
#include "satrar/aig/Circuit.hpp"
#include "satrar/aig/WireRef.hpp"
#include "satrar/analysis/GraphAnalysis.hpp"

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
    using satrar::aig::AagParser;
    using satrar::analysis::GraphAnalysis;

    const auto path = std::filesystem::path(__FILE__).parent_path() / "fixtures" / "and.aag";
    const auto circuit = AagParser::parseFile(path.string());
    GraphAnalysis g(circuit);

    const auto andId = circuit.findNodeByAigVar(3);
    CHECK(andId != satrar::aig::kInvalidNodeId);

    const auto fanin = g.faninCone(andId);
    CHECK(fanin.size() >= 3);

    satrar::aig::WireRef target{andId, 0};
    const auto doms = g.dominatorsForWire(target);
    CHECK(doms.size() == 1 && doms.front() == andId);

    const auto piId = circuit.findNodeByAigVar(1);
    CHECK(piId != satrar::aig::kInvalidNodeId);
    CHECK(g.wouldCreateCycle(andId, piId));

    const auto fanins = circuit.faninWires(andId);
    CHECK(fanins.size() == 2);
    CHECK(!g.wouldBypassWireCreateCycle(fanins[0], fanins[1]));
    CHECK(!g.wouldCreateCycle(piId, andId));

    std::cout << "test_graph: ok\n";
    return 0;
}
