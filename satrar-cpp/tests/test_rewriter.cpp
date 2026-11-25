#include "satrar/aig/AagParser.hpp"
#include "satrar/aig/AagWriter.hpp"
#include "satrar/analysis/Fault.hpp"
#include "satrar/analysis/FaultContext.hpp"
#include "satrar/rar/Rewriter.hpp"
#include "satrar/rar/SatRarEngine.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << " (" #cond ")\n";         \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

int main() {
    const auto fx = std::filesystem::path(__FILE__).parent_path() / "fixtures";
    auto golden = satrar::aig::AagParser::parseFile((fx / "and_self.aag").string());

    std::ostringstream dumped;
    satrar::aig::AagWriter::write(golden, dumped);
    const auto round = satrar::aig::AagParser::parseString(dumped.str(), "roundtrip");
    CHECK(round.stats().andGates == golden.stats().andGates);
    CHECK(round.evaluate({true}) == golden.evaluate({true}));

#if defined(SATRAR_HAS_MINISAT) && SATRAR_HAS_MINISAT
    const satrar::aig::NodeId andId = golden.findNodeByAigVar(2);
    satrar::analysis::PinFault fault{{andId, 0}, satrar::analysis::StuckAt::One};
    const satrar::rar::WireAlternative w{{fault, andId, "fixture"}, satrar::aig::Literal(2)};
    const auto patched = satrar::rar::Rewriter::applyWireAlternative(golden, w);
    const auto removed = satrar::rar::Rewriter::removeTargetAfterWire(golden, w);
    CHECK(patched.evaluate({true}) == golden.evaluate({true}));
    CHECK(removed.evaluate({true}) == golden.evaluate({true}));
    CHECK(patched.evaluate({false}) == golden.evaluate({false}));
    CHECK(removed.evaluate({false}) == golden.evaluate({false}));
    CHECK(patched.driverLiteral(fault.wire) == golden.driverLiteral(fault.wire));
#endif

    std::cout << "test_rewriter: ok\n";
    return 0;
}
