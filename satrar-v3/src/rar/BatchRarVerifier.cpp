#include "satrar/rar/BatchRarVerifier.hpp"

#include "satrar/aig/DenseAig.hpp"
#include "satrar/rar/Rewriter.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace satrar::rar {
namespace {

EquivalenceResult failure(EquivalenceVerdict verdict, const std::string& message) {
    EquivalenceResult result;
    result.verdict = verdict;
    result.message = message;
    return result;
}

constexpr std::uint64_t kAuditBit = 1ull << 62;

void decide(RarVerificationResult& result, RarStage stage, const EquivalenceResult& stageResult) {
    result.failedStage = stage;
    result.acceptance = stageResult.verdict == EquivalenceVerdict::NotEquivalent ? RarAcceptance::Rejected
                                                                                 : RarAcceptance::Unverified;
    result.message = std::string(rarStageName(stage)) + ": " + stageResult.message;
}

}  // namespace

BatchRarVerifier::BatchRarVerifier(std::shared_ptr<const aig::Circuit> golden, BatchVerifyOptions options,
                                   std::shared_ptr<const cec::CecChecker> checker)
    : golden_(std::move(golden)), options_(std::move(options)), checker_(std::move(checker)) {
    if (options_.batchSize == 0) options_.batchSize = 1;
    if (options_.workers == 0) options_.workers = 1;
    if (!golden_ || !checker_) throw std::invalid_argument("batch verifier needs a circuit and a checker");
    context_ = GoldenContext::build(*golden_);
    for (std::size_t index = 0; index < options_.workers; ++index) {
        workers_.emplace_back([this] { worker(); });
    }
}

BatchRarVerifier::~BatchRarVerifier() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        flush_ = true;
    }
    cv_.notify_all();
    for (auto& thread : workers_) {
        if (thread.joinable()) thread.join();
    }
}

std::uint64_t BatchRarVerifier::submit(Alternative alternative) {
    std::unique_lock<std::mutex> lock(mutex_);
    const auto limit = std::max<std::size_t>(options_.workers, 1) * options_.batchSize * 2;
    cv_.wait(lock, [&] { return pending_.size() < limit || stop_; });
    if (stop_) throw std::logic_error("batch verifier is closed");
    const auto ticket = nextTicket_++;
    pending_.emplace_back(ticket, std::move(alternative));
    cv_.notify_all();
    return ticket;
}

std::vector<BatchVerifyRow> BatchRarVerifier::takeReady() {
    std::vector<BatchVerifyRow> rows;
    while (true) {
        const auto found = ready_.find(nextEmit_);
        if (found == ready_.end()) break;
        rows.push_back(std::move(found->second));
        ready_.erase(found);
        ++nextEmit_;
    }
    return rows;
}

std::vector<BatchVerifyRow> BatchRarVerifier::poll() {
    std::lock_guard<std::mutex> lock(mutex_);
    return takeReady();
}

std::vector<BatchVerifyRow> BatchRarVerifier::drain() {
    std::unique_lock<std::mutex> lock(mutex_);
    flush_ = true;
    cv_.notify_all();
    cv_.wait(lock, [&] { return pending_.empty() && active_ == 0; });
    flush_ = false;
    return takeReady();
}

CecTiming BatchRarVerifier::timing() const noexcept {
    CecTiming timing;
    timing.buildNs = buildNs_.load();
    timing.writeNs = writeNs_.load();
    timing.abcNs = abcNs_.load();
    timing.coneAnds = coneAnds_.load();
    return timing;
}

void BatchRarVerifier::noteStats(const cec::CecBatchStats& stats) const {
    writeNs_ += stats.writeNs;
    abcNs_ += stats.abcNs;
    coneAnds_ += stats.coneAnds;
}

void BatchRarVerifier::worker() {
    CandidateBuilder builder(context_);
    for (;;) {
        std::vector<std::pair<std::uint64_t, Alternative>> batch;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] {
                return stop_ || pending_.size() >= options_.batchSize || (flush_ && !pending_.empty());
            });
            if (pending_.empty()) return;
            const auto count = std::min(options_.batchSize, pending_.size());
            batch.assign(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(count));
            pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(count));
            ++active_;
        }
        std::vector<std::uint64_t> tickets;
        tickets.reserve(batch.size());
        for (const auto& item : batch) tickets.push_back(item.first);
        std::vector<BatchVerifyRow> rows;
        try {
            rows = runBatch(std::move(batch), &builder);
        } catch (const std::exception& ex) {
            for (const auto ticket : tickets) {
                BatchVerifyRow row;
                row.ticket = ticket;
                row.result.addition = failure(EquivalenceVerdict::BackendError, ex.what());
                decide(row.result, RarStage::Addition, row.result.addition);
                rows.push_back(std::move(row));
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& row : rows) ready_.emplace(row.ticket, std::move(row));
            --active_;
        }
        cv_.notify_all();
    }
}

std::vector<BatchVerifyRow> BatchRarVerifier::runBatch(
    std::vector<std::pair<std::uint64_t, Alternative>> batch, CandidateBuilder* fast) const {
    const bool useFast = fast != nullptr && options_.builder == CandidateBuilderKind::Fast &&
                         options_.stages == CecStages::EndToEnd;
    struct Built {
        std::uint64_t ticket = 0;
        bool failed = false;
        bool cone = false;
        BatchVerifyRow row;
        std::shared_ptr<const aig::Circuit> added;
        std::shared_ptr<const aig::Circuit> removed;
        std::shared_ptr<const std::string> fullRight;
        std::uint32_t fullRightAnds = 0;
        cec::CecJob job;
        std::optional<cec::CecJob> audit;
    };
    std::vector<Built> built;
    built.reserve(batch.size());
    const auto buildStarted = std::chrono::steady_clock::now();
    for (auto& item : batch) {
        Built entry;
        entry.ticket = item.first;
        entry.row.ticket = item.first;
        try {
            if (useFast) {
                auto net = fast->build(item.second);
                const auto right = std::make_shared<const aig::DenseAig>(std::move(net.n2));
                const bool anyDirty = std::any_of(net.poDirty.begin(), net.poDirty.end(),
                                                   [](std::uint8_t bit) { return bit != 0; });
                const auto replay = [golden = context_, right](const std::vector<bool>& assignment) {
                    const auto left = golden->net.evaluate(assignment);
                    const auto candidate = right->evaluate(assignment);
                    for (std::size_t index = 0; index < left.size() && index < candidate.size(); ++index) {
                        if (left[index] != candidate[index]) return std::optional<std::size_t>(index);
                    }
                    return std::optional<std::size_t>{};
                };
                auto rightBytes = std::make_shared<const std::string>(right->toBinaryAiger());
                entry.job.id = entry.ticket;
                entry.job.inputs = context_->numInputs;
                entry.job.outputs = static_cast<std::uint32_t>(context_->outputs.size());
                entry.job.leftAig = context_->aiger;
                entry.job.leftAnds = context_->numAnds();
                entry.fullRight = rightBytes;
                entry.fullRightAnds = right->numAnds();
                entry.job.rightAig = std::move(rightBytes);
                entry.job.rightAnds = right->numAnds();
                entry.job.replayDifference = replay;
                if (options_.coneOnly && anyDirty) {
                    const auto leftCone = context_->net.selectOutputs(net.poDirty);
                    const auto rightCone = right->selectOutputs(net.poDirty);
                    entry.job.leftAig = std::make_shared<const std::string>(leftCone.toBinaryAiger());
                    entry.job.rightAig = std::make_shared<const std::string>(rightCone.toBinaryAiger());
                    entry.job.outputs = leftCone.numAnds() == 0 && leftCone.outputs.empty()
                                            ? 0
                                            : static_cast<std::uint32_t>(leftCone.outputs.size());
                    entry.job.leftAnds = leftCone.numAnds();
                    entry.job.rightAnds = rightCone.numAnds();
                    entry.cone = true;
                    const auto ordinal = ordinal_.fetch_add(1);
                    if (options_.auditEvery != 0 && ordinal % options_.auditEvery == 0) {
                        cec::CecJob full;
                        full.id = entry.ticket | kAuditBit;
                        full.inputs = context_->numInputs;
                        full.outputs = static_cast<std::uint32_t>(context_->outputs.size());
                        full.leftAig = context_->aiger;
                        full.leftAnds = context_->numAnds();
                        full.rightAig = std::make_shared<const std::string>(right->toBinaryAiger());
                        full.rightAnds = right->numAnds();
                        full.replayDifference = replay;
                        entry.audit = std::move(full);
                    }
                }
            } else {
                auto added = Rewriter::addAlternative(*golden_, item.second);
                const auto target = std::visit(
                    [](const auto& candidate) { return candidate.attachment.target; }, item.second);
                auto removed = Rewriter::removeTarget(added, target);
                entry.added = std::make_shared<const aig::Circuit>(std::move(added));
                entry.removed = std::make_shared<const aig::Circuit>(std::move(removed));
            }
        } catch (const std::exception& ex) {
            entry.failed = true;
            entry.row.result.addition = failure(EquivalenceVerdict::InvalidInput, ex.what());
            decide(entry.row.result, RarStage::Addition, entry.row.result.addition);
        }
        built.push_back(std::move(entry));
    }
    buildNs_ += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - buildStarted)
            .count());

    std::vector<cec::CecJob> jobs;
    std::vector<std::uint64_t> tickets;
    std::vector<bool> removal;
    for (const auto& entry : built) {
        if (entry.failed) continue;
        if (options_.stages == CecStages::EndToEnd) {
            if (useFast) {
                jobs.push_back(entry.job);
                if (entry.audit) jobs.push_back(*entry.audit);
            } else {
                jobs.push_back({entry.ticket, golden_, entry.removed});
            }
            tickets.push_back(entry.ticket);
            removal.push_back(false);
            continue;
        }
        jobs.push_back({entry.ticket * 2, golden_, entry.added});
        tickets.push_back(entry.ticket);
        removal.push_back(false);
        jobs.push_back({entry.ticket * 2 + 1, entry.added, entry.removed});
        tickets.push_back(entry.ticket);
        removal.push_back(true);
    }
    const auto started = std::chrono::steady_clock::now();
    if (!jobs.empty()) {
        ++batches_;
        abcJobs_ += jobs.size();
    }
    cec::CecBatchStats stats;
    const auto answers = jobs.empty() ? std::vector<EquivalenceResult>{}
                                      : checker_->checkBatch(jobs, options_.timeoutSeconds, &stats);
    noteStats(stats);
    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (answers.size() != jobs.size()) {
        throw std::runtime_error("checker returned the wrong number of results");
    }

    std::unordered_map<std::uint64_t, EquivalenceResult> direct;
    std::unordered_map<std::uint64_t, std::pair<EquivalenceResult, EquivalenceResult>> stages;
    for (std::size_t index = 0; index < answers.size(); ++index) {
        if (options_.stages == CecStages::EndToEnd) {
            direct.emplace(jobs[index].id, answers[index]);
        } else if (removal[index]) {
            stages[tickets[index]].second = answers[index];
        } else {
            stages[tickets[index]].first = answers[index];
        }
    }

    std::vector<BatchVerifyRow> rows;
    const auto share = built.empty() ? 0.0 : elapsed / static_cast<double>(built.size());
    for (auto& entry : built) {
        if (entry.failed) {
            rows.push_back(std::move(entry.row));
            continue;
        }
        auto& result = entry.row.result;
        result.verifySeconds = share;
        if (options_.stages == CecStages::EndToEnd) {
            result.addition = direct.at(entry.ticket);
            entry.row.endToEnd = result.addition.verdict;
            if (entry.audit) {
                const auto& audited = direct.at(entry.ticket | kAuditBit);
                const bool conflict =
                    (result.addition.verdict == EquivalenceVerdict::Equivalent) !=
                    (audited.verdict == EquivalenceVerdict::Equivalent);
                if (conflict && (result.addition.verdict == EquivalenceVerdict::Equivalent ||
                                 result.addition.verdict == EquivalenceVerdict::NotEquivalent) &&
                    (audited.verdict == EquivalenceVerdict::Equivalent ||
                     audited.verdict == EquivalenceVerdict::NotEquivalent)) {
                    ++coneMismatches_;
                    result.addition.verdict = EquivalenceVerdict::BackendError;
                    result.addition.message = "cec_cone_mismatch";
                    entry.row.endToEnd = result.addition.verdict;
                }
            }
            if (entry.cone && result.addition.verdict == EquivalenceVerdict::NotEquivalent) {
                cec::CecJob full = entry.job;
                full.id = entry.ticket | kAuditBit;
                full.leftAig = context_->aiger;
                full.leftAnds = context_->numAnds();
                full.rightAig = entry.fullRight;
                full.rightAnds = entry.fullRightAnds;
                full.outputs = static_cast<std::uint32_t>(context_->outputs.size());
                cec::CecBatchStats extra;
                const auto confirmed = checker_->checkBatch({full}, options_.timeoutSeconds, &extra);
                noteStats(extra);
                ++batches_;
                ++abcJobs_;
                if (confirmed.size() != 1 || confirmed[0].verdict != EquivalenceVerdict::NotEquivalent) {
                    if (!confirmed.empty() && confirmed[0].verdict == EquivalenceVerdict::Equivalent) {
                        ++coneMismatches_;
                    }
                    result.addition.verdict = EquivalenceVerdict::BackendError;
                    result.addition.message = "cec_cone_mismatch";
                    entry.row.endToEnd = result.addition.verdict;
                } else {
                    result.addition = confirmed[0];
                    entry.row.endToEnd = result.addition.verdict;
                }
            }
            std::string diagnosis;
            if (options_.diagnoseRejected &&
                result.addition.verdict == EquivalenceVerdict::NotEquivalent) {
                try {
                    const auto& candidate = batch[&entry - built.data()].second;
                    auto addedCircuit = Rewriter::addAlternative(*golden_, candidate);
                    const auto target = std::visit(
                        [](const auto& item) { return item.attachment.target; }, candidate);
                    auto removedCircuit = Rewriter::removeTarget(addedCircuit, target);
                    const auto added = std::make_shared<const aig::Circuit>(std::move(addedCircuit));
                    const auto removed = std::make_shared<const aig::Circuit>(std::move(removedCircuit));
                    cec::CecBatchStats extra;
                    const auto diag = checker_->checkBatch(
                        {{entry.ticket * 2, golden_, added}, {entry.ticket * 2 + 1, added, removed}},
                        options_.timeoutSeconds, &extra);
                    noteStats(extra);
                    ++batches_;
                    abcJobs_ += diag.size();
                    if (diag.size() == 2) {
                        diagnosis = std::string(" diag addition=") + equivalenceVerdictName(diag[0].verdict) +
                                    " removal=" + equivalenceVerdictName(diag[1].verdict);
                    }
                } catch (const std::exception& ex) {
                    diagnosis = std::string(" diag error=") + ex.what();
                }
            }
            if (result.addition.verdict == EquivalenceVerdict::Equivalent) {
                result.acceptance = RarAcceptance::Accepted;
                result.failedStage = RarStage::None;
                result.message = result.addition.message;
            } else {
                decide(result, RarStage::EndToEnd, result.addition);
            }
            if (!diagnosis.empty()) result.message += diagnosis;
        } else {
            const auto& stage = stages.at(entry.ticket);
            result.addition = stage.first;
            result.removal = stage.second;
            if (result.addition.verdict == EquivalenceVerdict::Equivalent &&
                result.removal->verdict == EquivalenceVerdict::Equivalent) {
                result.acceptance = RarAcceptance::Accepted;
                result.failedStage = RarStage::None;
                result.message = result.removal->message;
            } else if (result.addition.verdict != EquivalenceVerdict::Equivalent) {
                decide(result, RarStage::Addition, result.addition);
            } else {
                decide(result, RarStage::Removal, *result.removal);
            }
        }
        rows.push_back(std::move(entry.row));
    }
    return rows;
}

}  // namespace satrar::rar
