#pragma once

#include "satrar/cec/CecChecker.hpp"
#include "satrar/rar/CandidateBuilder.hpp"
#include "satrar/rar/RarVerifier.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace satrar::rar {

enum class CecStages { TwoStage, EndToEnd };
enum class CandidateBuilderKind { Reference, Fast };

struct BatchVerifyOptions {
    CecStages stages = CecStages::EndToEnd;
    CandidateBuilderKind builder = CandidateBuilderKind::Fast;
    bool coneOnly = false;
    /// When coneOnly is set, also check one full-circuit job every N candidates. 0 disables it.
    std::size_t auditEvery = 0;
    /// On an e2e rejection, also run the two-stage reference check and append it to the message.
    bool diagnoseRejected = false;
    std::size_t batchSize = 64;
    std::size_t workers = 1;
    std::uint32_t timeoutSeconds = 30;
};

struct CecTiming {
    std::uint64_t buildNs = 0;
    std::uint64_t writeNs = 0;
    std::uint64_t abcNs = 0;
    std::uint64_t coneAnds = 0;
};

struct BatchVerifyRow {
    std::uint64_t ticket = 0;
    RarVerificationResult result;
    std::optional<EquivalenceVerdict> endToEnd;
};

/// Verifies candidates with one ABC process per batch. poll() returns ticket order.
class BatchRarVerifier {
public:
    BatchRarVerifier(std::shared_ptr<const aig::Circuit> golden, BatchVerifyOptions options,
                     std::shared_ptr<const cec::CecChecker> checker);
    ~BatchRarVerifier();

    BatchRarVerifier(const BatchRarVerifier&) = delete;
    BatchRarVerifier& operator=(const BatchRarVerifier&) = delete;

    std::uint64_t submit(Alternative alternative);
    [[nodiscard]] std::vector<BatchVerifyRow> poll();
    [[nodiscard]] std::vector<BatchVerifyRow> drain();
    [[nodiscard]] std::size_t batchesRun() const noexcept { return batches_.load(); }
    [[nodiscard]] std::size_t abcJobs() const noexcept { return abcJobs_.load(); }
    [[nodiscard]] CecTiming timing() const noexcept;
    [[nodiscard]] std::uint64_t coneMismatches() const noexcept { return coneMismatches_.load(); }

private:
    void worker();
    [[nodiscard]] std::vector<BatchVerifyRow> runBatch(
        std::vector<std::pair<std::uint64_t, Alternative>> batch, CandidateBuilder* fast) const;
    [[nodiscard]] std::vector<BatchVerifyRow> takeReady();
    void noteStats(const cec::CecBatchStats& stats) const;

    std::shared_ptr<const aig::Circuit> golden_;
    std::shared_ptr<const GoldenContext> context_;
    BatchVerifyOptions options_;
    std::shared_ptr<const cec::CecChecker> checker_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::pair<std::uint64_t, Alternative>> pending_;
    std::map<std::uint64_t, BatchVerifyRow> ready_;
    std::uint64_t nextTicket_ = 0;
    std::uint64_t nextEmit_ = 0;
    std::size_t active_ = 0;
    bool flush_ = false;
    bool stop_ = false;
    mutable std::atomic<std::size_t> batches_{0};
    mutable std::atomic<std::size_t> abcJobs_{0};
    mutable std::atomic<std::uint64_t> buildNs_{0};
    mutable std::atomic<std::uint64_t> writeNs_{0};
    mutable std::atomic<std::uint64_t> abcNs_{0};
    mutable std::atomic<std::uint64_t> coneAnds_{0};
    mutable std::atomic<std::uint64_t> ordinal_{0};
    mutable std::atomic<std::uint64_t> coneMismatches_{0};
    std::vector<std::thread> workers_;
};

}  // namespace satrar::rar
