#include "satrar/aig/AagParser.hpp"
#include "satrar/cec/AbcCec.hpp"
#include "satrar/cec/CecChecker.hpp"
#include "satrar/rar/BatchRarVerifier.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <thread>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d (%s)\n", __FILE__, __LINE__, #cond);            \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

namespace {

class Always final : public satrar::cec::CecChecker {
public:
    bool available() const override { return true; }
    std::string describe() const override { return "always"; }
    std::vector<satrar::rar::EquivalenceResult> checkBatch(const std::vector<satrar::cec::CecJob>& jobs,
                                                           std::uint32_t, satrar::cec::CecBatchStats*) const override {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        satrar::rar::EquivalenceResult result;
        result.verdict = satrar::rar::EquivalenceVerdict::Equivalent;
        result.message = "scripted";
        return {jobs.size(), result};
    }
};

std::filesystem::path labRoot() {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

std::vector<std::uint64_t> run(std::size_t workers, const satrar::aig::Circuit& circuit,
                               const satrar::rar::WireAlternative& alternative) {
    satrar::rar::BatchVerifyOptions options;
    options.batchSize = 2;
    options.workers = workers;
    auto checker = std::make_shared<Always>();
    satrar::rar::BatchRarVerifier verifier(std::make_shared<satrar::aig::Circuit>(circuit), options, checker);
    std::vector<std::uint64_t> tickets;
    for (int i = 0; i < 5; ++i) tickets.push_back(verifier.submit(alternative));
    std::vector<std::uint64_t> order;
    for (const auto& row : verifier.drain()) {
        if (row.result.acceptance != satrar::rar::RarAcceptance::Accepted) {
            throw std::runtime_error("scripted batch did not accept");
        }
        order.push_back(row.ticket);
    }
    if (order != tickets) throw std::runtime_error("batch results were not in ticket order");
    return order;
}

}  // namespace

int main() try {
    const auto golden = satrar::aig::AagParser::parseFile(
        (labRoot() / "satrar-cpp/tests/fixtures/and_self.aag").string());
    const auto andId = golden.findNodeByAigVar(2);
    const satrar::analysis::PinFault fault{{andId, 0}, satrar::analysis::StuckAt::One};
    const satrar::rar::WireAlternative good{{fault, andId, "fixture"}, satrar::aig::Literal(2)};
    const auto one = run(1, golden, good);
    const auto four = run(4, golden, good);
    CHECK(one == four);

    satrar::cec::AbcCec abc;
    if (!abc.available()) {
        std::fprintf(stderr, "test_batch_verifier: scripted ok, ABC skipped\n");
        return 0;
    }
    satrar::rar::BatchVerifyOptions options;
    options.stages = satrar::rar::CecStages::TwoStage;
    options.builder = satrar::rar::CandidateBuilderKind::Reference;
    options.batchSize = 2;
    options.workers = 2;
    options.timeoutSeconds = 60;
    satrar::rar::BatchRarVerifier verifier(std::make_shared<satrar::aig::Circuit>(golden), options,
                                           std::make_shared<satrar::cec::AbcCec>(abc));
    auto wrong = good;
    wrong.attachment.target.stuckAt = satrar::analysis::StuckAt::Zero;
    const auto goodTicket = verifier.submit(good);
    const auto badTicket = verifier.submit(wrong);
    const auto rows = verifier.drain();
    CHECK(rows.size() == 2);
    CHECK(rows[0].ticket == goodTicket && rows[0].result.acceptance == satrar::rar::RarAcceptance::Accepted);
    CHECK(rows[1].ticket == badTicket && rows[1].result.acceptance == satrar::rar::RarAcceptance::Rejected);
    CHECK(rows[1].result.removal && rows[1].result.removal->counterexampleReplayOk);
    std::fprintf(stderr, "test_batch_verifier: ok\n");
    return 0;
} catch (const std::exception& ex) {
    std::fprintf(stderr, "FAIL %s\n", ex.what());
    return 1;
}
