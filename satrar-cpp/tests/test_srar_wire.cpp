#include "satrar/aig/AagParser.hpp"
#include "satrar/analysis/Fault.hpp"
#include "satrar/analysis/FaultContext.hpp"
#include "satrar/rar/SatRarEngine.hpp"

#include <cstdio>
#include <filesystem>

int main() {
#ifndef SATRAR_HAS_MINISAT
    std::fprintf(stderr, "test_srar_wire: skipped (no MiniSat)\n");
    return 0;
#else
    const auto path =
        std::filesystem::path(__FILE__).parent_path() / "fixtures" / "and.aag";
    auto circuit = satrar::aig::AagParser::parseFile(path.string());
    const satrar::aig::NodeId andId = circuit.findNodeByAigVar(3);

    satrar::analysis::FaultContext ctx(std::move(circuit));
    satrar::analysis::PinFault fault{{andId, 0}, satrar::analysis::StuckAt::Zero};

    satrar::rar::SatRarEngine engine(ctx);
    satrar::rar::SatRarOptions opts;
    opts.mode = satrar::rar::SatRarMode::SrarWire;
    engine.setOptions(opts);
    engine.setPinTarget(fault);
    const auto alts = engine.run();

    std::fprintf(stderr, "test_srar_wire: ok (#alts=%zu)\n", alts.size());
    return 0;
#endif
}