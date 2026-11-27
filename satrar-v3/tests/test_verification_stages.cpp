#include "satrar/aig/AagParser.hpp"
#include "satrar/cec/AbcCec.hpp"
#include "satrar/cec/CecChecker.hpp"
#include "satrar/rar/ExportAag.hpp"
#include "satrar/rar/RarVerifier.hpp"

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

namespace {

class ScriptedChecker final : public satrar::cec::CecChecker {
public:
    using Script = std::function<satrar::rar::EquivalenceResult(int)>;
    ScriptedChecker(Script script, bool usable = true) : script_(std::move(script)), usable_(usable) {}
    bool available() const override { return usable_; }
    std::string describe() const override { return "scripted"; }
    std::vector<satrar::rar::EquivalenceResult> checkBatch(const std::vector<satrar::cec::CecJob>& jobs,
                                                           std::uint32_t, satrar::cec::CecBatchStats*) const override {
        std::vector<satrar::rar::EquivalenceResult> results;
        for (std::size_t i = 0; i < jobs.size(); ++i) {
            if (!usable_) {
                satrar::rar::EquivalenceResult result;
                result.verdict = satrar::rar::EquivalenceVerdict::BackendError;
                result.message = "unavailable";
                results.push_back(result);
                continue;
            }
            results.push_back(script_(calls_++));
        }
        return results;
    }

private:
    Script script_;
    bool usable_;
    mutable std::atomic<int> calls_{0};
};

satrar::rar::EquivalenceResult verdict(satrar::rar::EquivalenceVerdict value) {
    satrar::rar::EquivalenceResult result;
    result.verdict = value;
    result.message = satrar::rar::equivalenceVerdictName(value);
    return result;
}

std::string slurp(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

std::filesystem::path labRoot() {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

}  // namespace

int main() {
    const auto golden = satrar::aig::AagParser::parseFile(
        (labRoot() / "satrar-cpp/tests/fixtures/and_self.aag").string());
    const auto andId = golden.findNodeByAigVar(2);
    const satrar::analysis::PinFault fault{{andId, 0}, satrar::analysis::StuckAt::One};
    const satrar::rar::WireAlternative good{{fault, andId, "fixture"}, satrar::aig::Literal(2)};

    {
        ScriptedChecker scripted([](int) { return verdict(satrar::rar::EquivalenceVerdict::Equivalent); });
        const auto result = satrar::rar::RarVerifier(scripted).verify(golden, good, 30);
        CHECK(result.acceptance == satrar::rar::RarAcceptance::Accepted);
        CHECK(result.rewritten.has_value());
    }
    {
        ScriptedChecker scripted([](int call) {
            return verdict(call == 0 ? satrar::rar::EquivalenceVerdict::Equivalent
                                     : satrar::rar::EquivalenceVerdict::Timeout);
        });
        const auto result = satrar::rar::RarVerifier(scripted).verify(golden, good, 30);
        CHECK(result.addition.verdict == satrar::rar::EquivalenceVerdict::Equivalent);
        CHECK(result.removal && result.removal->verdict == satrar::rar::EquivalenceVerdict::Timeout);
        CHECK(result.acceptance == satrar::rar::RarAcceptance::Unverified);
        CHECK(result.failedStage == satrar::rar::RarStage::Removal);
        CHECK(!result.rewritten);
    }
    {
        ScriptedChecker unavailable([](int) { return verdict(satrar::rar::EquivalenceVerdict::Error); }, false);
        const auto result = satrar::rar::RarVerifier(unavailable).verify(golden, good, 30);
        CHECK(result.acceptance == satrar::rar::RarAcceptance::Unverified);
        CHECK(result.failedStage == satrar::rar::RarStage::Addition);
    }
    {
        auto cyclic = good;
        cyclic.source = satrar::aig::Literal(2 * 99);
        ScriptedChecker scripted([](int) { return verdict(satrar::rar::EquivalenceVerdict::Equivalent); });
        const auto result = satrar::rar::RarVerifier(scripted).verify(golden, cyclic, 30);
        CHECK(result.acceptance == satrar::rar::RarAcceptance::Unverified);
        CHECK(result.addition.verdict == satrar::rar::EquivalenceVerdict::InvalidInput);
        CHECK(!result.removal);
    }

    satrar::cec::AbcCec abc;
    if (!abc.available()) {
        std::fprintf(stderr, "test_verification_stages: scripted ok, ABC skipped\n");
        return 0;
    }
    auto wrong = good;
    wrong.attachment.target.stuckAt = satrar::analysis::StuckAt::Zero;
    const auto rejected = satrar::rar::RarVerifier(abc).verify(golden, wrong, 60);
    CHECK(rejected.addition.verdict == satrar::rar::EquivalenceVerdict::Equivalent);
    CHECK(rejected.removal && rejected.removal->verdict == satrar::rar::EquivalenceVerdict::NotEquivalent);
    CHECK(rejected.removal->counterexampleReplayOk);
    CHECK(rejected.acceptance == satrar::rar::RarAcceptance::Rejected);

    const auto accepted = satrar::rar::RarVerifier(abc).verify(golden, good, 60);
    CHECK(accepted.acceptance == satrar::rar::RarAcceptance::Accepted);
    const auto directory = std::filesystem::temp_directory_path() / "satrar-v3-export";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto destination = directory / "out.aag";
    {
        std::ofstream old(destination, std::ios::binary);
        old << "previous contents";
    }
    satrar::rar::ExportOptions options;
    options.timeoutSeconds = 60;
    options.beforePublish = [](const std::filesystem::path&) { throw std::runtime_error("disk full"); };
    auto failed = satrar::rar::exportVerifiedAag(golden, *accepted.rewritten, destination, abc, options);
    CHECK(!failed.published);
    CHECK(slurp(destination) == "previous contents");
    options.beforePublish = nullptr;
    const auto published = satrar::rar::exportVerifiedAag(golden, *accepted.rewritten, destination, abc, options);
    CHECK(published.published);
    CHECK(slurp(destination) != "previous contents");
    std::filesystem::remove_all(directory);
    std::fprintf(stderr, "test_verification_stages: ok\n");
    return 0;
}
