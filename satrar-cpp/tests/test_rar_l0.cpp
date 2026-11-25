#include "satrar/aig/AagParser.hpp"
#include "satrar/analysis/Fault.hpp"
#include "satrar/analysis/FaultContext.hpp"
#include "satrar/rar/Alternative.hpp"
#include "satrar/rar/SatRarEngine.hpp"
#include "satrar/sat/MinisatPropagationEngine.hpp"

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
#ifndef SATRAR_HAS_MINISAT
    std::fprintf(stderr, "test_rar_l0: skipped (no MiniSat)\n");
    return 0;
#else
    CHECK(satrar::sat::MinisatPropagationEngine::runSmokeSelfTest());

    const auto path = std::filesystem::path(__FILE__).parent_path() / "fixtures" / "and.aag";
    auto circuit = satrar::aig::AagParser::parseFile(path.string());
    const satrar::aig::NodeId andId = circuit.findNodeByAigVar(3);
    CHECK(andId != satrar::aig::kInvalidNodeId);

    satrar::analysis::FaultContext ctx(std::move(circuit));
    satrar::analysis::PinFault fault{{andId, 0}, satrar::analysis::StuckAt::Zero};
    ctx.setPinFault(fault);

    satrar::rar::SatRarEngine engine(ctx);
    satrar::rar::SatRarOptions opts;
    opts.mode = satrar::rar::SatRarMode::TwoWayRarL0;
    engine.setOptions(opts);
    engine.setPinTarget(fault);

    const auto alts = engine.run();
    for (const auto& item : alts) {
        if (std::holds_alternative<satrar::rar::WireAlternative>(item)) {
            const auto& w = std::get<satrar::rar::WireAlternative>(item);
            CHECK(w.attachment.target.wire == fault.wire);
            CHECK(w.attachment.dominator != satrar::aig::kInvalidNodeId);
        }
    }

    std::fprintf(stderr, "test_rar_l0: ok (#alts=%zu)\n", alts.size());
    return 0;
#endif
}
