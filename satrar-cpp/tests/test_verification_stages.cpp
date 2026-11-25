// R1: the two-stage CEC keeps stage evidence and never publishes an undecided result.
#include "satrar/aig/AagParser.hpp"
#include "satrar/analysis/Fault.hpp"
#include "satrar/rar/Alternative.hpp"
#include "satrar/rar/ExportAag.hpp"
#include "satrar/rar/RarVerifier.hpp"
#include "satrar/sat/CadicalBackend.hpp"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d (%s)\n", __FILE__, __LINE__, #cond);            \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

using namespace satrar;

namespace {

/// Delegates the first \c honest calls to a real solver, then behaves as scripted.
class ScriptedBackend final : public sat::SatBackend {
public:
    using Script = std::function<sat::SatSolveResult(int call)>;
    ScriptedBackend(const sat::SatBackend& real, int honest, Script script, bool available = true)
        : real_(real), honest_(honest), script_(std::move(script)), available_(available) {}

    bool available() const override { return available_ && real_.available(); }
    std::string describe() const override { return "scripted"; }
    sat::SatSolveResult solveDimacsFile(const std::filesystem::path& path,
                                        std::optional<std::uint32_t> timeout) const override {
        const int call = calls_++;
        if (call < honest_) {
            return real_.solveDimacsFile(path, timeout);
        }
        return script_(call);
    }
    int calls() const { return calls_; }

private:
    const sat::SatBackend& real_;
    int honest_;
    Script script_;
    bool available_;
    mutable std::atomic<int> calls_{0};
};

std::string slurp(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

}  // namespace

int main() {
    sat::CadicalBackend real;
    if (!real.available()) {
        std::fprintf(stderr, "test_verification_stages: SKIP (cadical unavailable)\n");
        return 77;
    }
    const auto fx = std::filesystem::path(__FILE__).parent_path() / "fixtures";
    const auto golden = aig::AagParser::parseFile((fx / "and_self.aag").string());
    const auto andId = golden.findNodeByAigVar(2);
    const analysis::PinFault fault{{andId, 0}, analysis::StuckAt::One};
    const rar::WireAlternative good{{fault, andId, "fixture"}, aig::Literal(2)};

    // Reference: both stages honest => accepted with stage evidence.
    {
        const auto result = rar::RarVerifier(real).verify(golden, good, 60u);
        CHECK(result.acceptance == rar::RarAcceptance::Accepted);
        CHECK(result.failedStage == rar::RarStage::None);
        CHECK(result.addition.verdict == rar::EquivalenceVerdict::Equivalent);
        CHECK(result.removal && result.removal->verdict == rar::EquivalenceVerdict::Equivalent);
        CHECK(result.rewritten.has_value());
    }

    // Failure in the second stage keeps the first stage's evidence and never accepts.
    struct Case { const char* name; ScriptedBackend::Script script; rar::EquivalenceVerdict verdict; };
    const std::vector<Case> cases = {
        {"throws", [](int) -> sat::SatSolveResult { throw std::runtime_error("injected"); },
         rar::EquivalenceVerdict::BackendError},
        {"timeout", [](int) { sat::SatSolveResult r; r.verdict = sat::SatVerdict::Timeout; return r; },
         rar::EquivalenceVerdict::Timeout},
        {"unknown", [](int) { sat::SatSolveResult r; r.verdict = sat::SatVerdict::Unknown; return r; },
         rar::EquivalenceVerdict::Unknown},
        {"backend error", [](int) { sat::SatSolveResult r; r.verdict = sat::SatVerdict::Error; return r; },
         rar::EquivalenceVerdict::BackendError},
        // SAT without a model cannot be replayed; it must not become a rejection either.
        {"sat without model", [](int) { sat::SatSolveResult r; r.verdict = sat::SatVerdict::Sat; return r; },
         rar::EquivalenceVerdict::BackendError},
        // Model literal out of range => corrupted output.
        {"sat corrupt model", [](int) {
             sat::SatSolveResult r;
             r.verdict = sat::SatVerdict::Sat;
             r.modelLits = {0};
             return r;
         }, rar::EquivalenceVerdict::BackendError},
    };
    for (const auto& item : cases) {
        const ScriptedBackend backend(real, 1, item.script);
        const auto result = rar::RarVerifier(backend).verify(golden, good, 60u);
        if (result.addition.verdict != rar::EquivalenceVerdict::Equivalent) {
            std::fprintf(stderr, "case '%s': addition evidence lost\n", item.name);
            return 1;
        }
        if (!result.removal || result.removal->verdict != item.verdict) {
            std::fprintf(stderr, "case '%s': removal verdict mismatch\n", item.name);
            return 1;
        }
        CHECK(result.acceptance == rar::RarAcceptance::Unverified);
        CHECK(result.failedStage == rar::RarStage::Removal);
        CHECK(!result.rewritten.has_value());
        CHECK(backend.calls() == 2);
    }

    // First stage failure: second stage is not attempted.
    {
        const ScriptedBackend backend(real, 0, [](int) { sat::SatSolveResult r; r.verdict = sat::SatVerdict::Timeout; return r; });
        const auto result = rar::RarVerifier(backend).verify(golden, good, 60u);
        CHECK(result.acceptance == rar::RarAcceptance::Unverified);
        CHECK(result.failedStage == rar::RarStage::Addition);
        CHECK(!result.removal.has_value());
        CHECK(backend.calls() == 1);
    }

    // Unavailable backend is never a rejection.
    {
        const ScriptedBackend backend(real, 0, [](int) { return sat::SatSolveResult{}; }, false);
        const auto result = rar::RarVerifier(backend).verify(golden, good, 60u);
        CHECK(result.acceptance == rar::RarAcceptance::Unverified);
        CHECK(result.addition.verdict == rar::EquivalenceVerdict::BackendError);
    }

    // A real functional difference in the removal stage is Rejected and keeps stage 1.
    {
        auto wrong = good;
        wrong.attachment.target.stuckAt = analysis::StuckAt::Zero;
        const auto result = rar::RarVerifier(real).verify(golden, wrong, 60u);
        CHECK(result.addition.verdict == rar::EquivalenceVerdict::Equivalent);
        CHECK(result.removal && result.removal->verdict == rar::EquivalenceVerdict::NotEquivalent);
        CHECK(result.removal->counterexampleReplayOk);
        CHECK(result.acceptance == rar::RarAcceptance::Rejected);
        CHECK(result.failedStage == rar::RarStage::Removal);
    }

    // Construction failure (cyclic/unknown source) is attributed to the addition stage.
    {
        auto cyclic = good;
        cyclic.source = aig::Literal(2 * 99);  // no such variable
        const auto result = rar::RarVerifier(real).verify(golden, cyclic, 60u);
        CHECK(result.acceptance == rar::RarAcceptance::Unverified);
        CHECK(result.failedStage == rar::RarStage::Addition);
        CHECK(result.addition.verdict == rar::EquivalenceVerdict::InvalidInput);
        CHECK(!result.removal.has_value());
    }

    // Export: atomic publish, round-trip proof, old destination preserved on every failure.
    {
        const auto accepted = rar::RarVerifier(real).verify(golden, good, 60u);
        CHECK(accepted.rewritten.has_value());
        const auto directory = std::filesystem::temp_directory_path() / "satrar-export-test";
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        const auto destination = directory / "out.aag";
        const auto countFiles = [&] {
            std::size_t n = 0;
            for (const auto& entry : std::filesystem::directory_iterator(directory)) { (void)entry; ++n; }
            return n;
        };
        {
            std::ofstream old(destination, std::ios::binary);
            old << "previous contents";
        }
        rar::ExportOptions options;
        options.timeoutSeconds = 60u;

        // hook failure => not published, no temp left, old file intact
        options.beforePublish = [](const std::filesystem::path&) { throw std::runtime_error("disk full"); };
        auto failed = rar::exportVerifiedAag(golden, *accepted.rewritten, destination, real, options);
        CHECK(!failed.published);
        CHECK(slurp(destination) == "previous contents");
        CHECK(countFiles() == 1);

        // round-trip CEC failure (rewrite is not equivalent) => not published
        auto broken = golden.clone();
        broken.replaceAndFanin(andId, 0, aig::Literal(0));
        options.beforePublish = nullptr;
        failed = rar::exportVerifiedAag(golden, broken, destination, real, options);
        CHECK(!failed.published);
        CHECK(failed.roundTrip == rar::EquivalenceVerdict::NotEquivalent);
        CHECK(slurp(destination) == "previous contents");
        CHECK(countFiles() == 1);

        // undecidable round trip (backend timeout) => not published
        {
            const ScriptedBackend timeout(real, 0, [](int) { sat::SatSolveResult r; r.verdict = sat::SatVerdict::Timeout; return r; });
            failed = rar::exportVerifiedAag(golden, *accepted.rewritten, destination, timeout, options);
            CHECK(!failed.published);
            CHECK(failed.roundTrip == rar::EquivalenceVerdict::Timeout);
            CHECK(slurp(destination) == "previous contents");
        }

        // missing output directory => failure, nothing created
        failed = rar::exportVerifiedAag(golden, *accepted.rewritten, directory / "missing" / "out.aag", real, options);
        CHECK(!failed.published);
        CHECK(countFiles() == 1);

        // success replaces the old file with a loadable, equivalent AIG
        const auto ok = rar::exportVerifiedAag(golden, *accepted.rewritten, destination, real, options);
        CHECK(ok.published);
        CHECK(countFiles() == 1);
        const auto reloaded = aig::AagParser::parseFile(destination.string());
        CHECK(reloaded.evaluate({true}) == golden.evaluate({true}));
        CHECK(reloaded.evaluate({false}) == golden.evaluate({false}));
        std::filesystem::remove_all(directory);
    }

    std::fprintf(stderr, "test_verification_stages: ok\n");
    return 0;
}
