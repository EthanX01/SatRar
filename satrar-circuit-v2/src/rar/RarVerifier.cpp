
#include "satrar/rar/RarVerifier.hpp"
#include "satrar/rar/Rewriter.hpp"

#include <chrono>

namespace satrar::rar {

const char* rarAcceptanceName(RarAcceptance acceptance) noexcept {
    switch (acceptance) {
        case RarAcceptance::Accepted: return "accepted";
        case RarAcceptance::Rejected: return "rejected";
        case RarAcceptance::Unverified: return "indeterminate";
    }
    return "indeterminate";
}

const char* rarStageName(RarStage stage) noexcept {
    switch (stage) {
        case RarStage::None: return "none";
        case RarStage::Addition: return "addition";
        case RarStage::Removal: return "removal";
    }
    return "none";
}

namespace {

EquivalenceResult failure(EquivalenceVerdict verdict, const std::string& message) {
    EquivalenceResult result;
    result.verdict = verdict;
    result.message = message;
    return result;
}

/// Run one CEC stage without letting an exception escape or overwrite earlier evidence.
EquivalenceResult checkStage(const EquivalenceChecker& checker, const aig::Circuit& left,
                             const aig::Circuit& right, std::optional<std::uint32_t> timeout) {
    try {
        return checker.check(left, right, timeout);
    } catch (const std::exception& ex) {
        return failure(EquivalenceVerdict::BackendError, ex.what());
    }
}

}  // namespace

RarVerificationResult RarVerifier::verify(const aig::Circuit& golden, const Alternative& alt,
                                           std::optional<std::uint32_t> timeout) const {
    RarVerificationResult result;
    const auto start = std::chrono::steady_clock::now();
    const auto finish = [&] {
        result.verifySeconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        return std::move(result);
    };
    const auto decide = [&](RarStage stage, const EquivalenceResult& stageResult) {
        result.failedStage = stage;
        result.acceptance = stageResult.verdict == EquivalenceVerdict::NotEquivalent
                                ? RarAcceptance::Rejected
                                : RarAcceptance::Unverified;
        result.message = std::string(rarStageName(stage)) + ": " + stageResult.message;
    };

    const EquivalenceChecker checker(backend_);

    // Stage 1: N0 == N1 (alternative added, target pin still present).
    std::optional<aig::Circuit> added;
    try {
        added = Rewriter::addAlternative(golden, alt);
    } catch (const std::exception& ex) {
        result.addition = failure(EquivalenceVerdict::InvalidInput, ex.what());
        decide(RarStage::Addition, result.addition);
        return finish();
    }
    result.addition = checkStage(checker, golden, *added, timeout);
    if (result.addition.verdict != EquivalenceVerdict::Equivalent) {
        decide(RarStage::Addition, result.addition);
        return finish();
    }

    // Stage 2: N1 == N2 (target pin tied to its stuck value, then simplified).
    std::optional<aig::Circuit> removed;
    try {
        const auto target = std::visit([](const auto& item) { return item.attachment.target; }, alt);
        removed = Rewriter::removeTarget(*added, target);
    } catch (const std::exception& ex) {
        result.removal = failure(EquivalenceVerdict::InvalidInput, ex.what());
        decide(RarStage::Removal, *result.removal);
        return finish();
    }
    result.removal = checkStage(checker, *added, *removed, timeout);
    if (result.removal->verdict != EquivalenceVerdict::Equivalent) {
        decide(RarStage::Removal, *result.removal);
        return finish();
    }

    result.acceptance = RarAcceptance::Accepted;
    result.failedStage = RarStage::None;
    result.rewritten = std::move(removed);
    result.message = result.removal->message;
    return finish();
}

}  // namespace satrar::rar
