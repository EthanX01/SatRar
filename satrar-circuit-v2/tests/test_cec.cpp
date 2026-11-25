#include "satrar/aig/AagParser.hpp"
#include "satrar/analysis/FaultContext.hpp"
#include "satrar/rar/ExportAag.hpp"
#include "satrar/rar/RarVerifier.hpp"
#include "satrar/rar/SatRarEngine.hpp"
#include "satrar/sat/CadicalBackend.hpp"

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
    const satrar::sat::CadicalBackend backend;
    if (!backend.available()) {
        std::fprintf(stderr, "test_cec: skip, CaDiCaL unavailable\n");
        return 77;
    }
    const auto path = std::filesystem::path(__FILE__).parent_path() / "fixtures/paper/fig1.aag";
    const auto circuit = satrar::aig::AagParser::parseFile(path.string());
    satrar::analysis::FaultContext context(circuit.clone());
    satrar::rar::SatRarEngine engine(context);
    satrar::rar::SatRarOptions options;
    options.mode = satrar::rar::SatRarMode::SrarWire;
    engine.setOptions(options);
    const auto g7 = circuit.findNodeByAigVar(13);
    engine.setPinTarget({{g7, 0}, satrar::analysis::StuckAt::One});
    const auto found = engine.run();
    CHECK(!found.empty());
    const satrar::rar::RarVerifier verifier(backend);
    std::size_t accepted = 0;
    for (const auto& alt : found) {
        const auto result = verifier.verify(circuit, alt, 10);
        CHECK(result.acceptance == satrar::rar::RarAcceptance::Accepted);
        CHECK(result.addition.verdict == satrar::rar::EquivalenceVerdict::Equivalent);
        CHECK(result.removal && result.removal->verdict == satrar::rar::EquivalenceVerdict::Equivalent);
        ++accepted;
    }
    CHECK(accepted == found.size());
    const auto destination = std::filesystem::temp_directory_path() / "satrar-v2-fig1-rewritten.aag";
    satrar::rar::ExportOptions exportOptions;
    exportOptions.timeoutSeconds = 10;
    const auto published = satrar::rar::exportVerifiedAag(circuit, *verifier.verify(circuit, found.front(), 10).rewritten,
                                                          destination, backend, exportOptions);
    CHECK(published.published);
    const auto reloaded = satrar::aig::AagParser::parseFile(destination.string());
    CHECK(reloaded.primaryInputIds().size() == circuit.primaryInputIds().size());
    std::filesystem::remove(destination);
    std::fprintf(stderr, "test_cec: ok accepted=%zu\n", accepted);
    return 0;
}
