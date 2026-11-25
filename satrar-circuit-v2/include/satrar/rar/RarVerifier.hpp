
#pragma once

#include "satrar/rar/Alternative.hpp"
#include "satrar/rar/EquivalenceChecker.hpp"

namespace satrar::rar {

/// Accepted: N0==N1 and N1==N2. Rejected: a stage produced a replayed functional difference.
/// Unverified (spec "Indeterminate"): every other outcome (timeout, backend/input failure, unknown).
enum class RarAcceptance { Accepted, Rejected, Unverified };

/// First stage that did not produce a trusted equivalence.
enum class RarStage { None, Addition, Removal };

[[nodiscard]] const char* rarAcceptanceName(RarAcceptance acceptance) noexcept;
[[nodiscard]] const char* rarStageName(RarStage stage) noexcept;

struct RarVerificationResult {
    RarAcceptance acceptance = RarAcceptance::Unverified;
    /// Stage that decided a non-accepted result (None when accepted).
    RarStage failedStage = RarStage::None;
    /// N0 == N1. Always preserved, even when the removal stage fails.
    EquivalenceResult addition;
    /// N1 == N2. Set once the removal stage was attempted (including construction failure).
    std::optional<EquivalenceResult> removal;
    std::optional<aig::Circuit> rewritten;
    std::string message;
    double verifySeconds = 0.0;
};

class RarVerifier {
public:
    explicit RarVerifier(const sat::SatBackend& backend) : backend_(backend) {}
    [[nodiscard]] RarVerificationResult verify(
        const aig::Circuit& golden, const Alternative& alt,
        std::optional<std::uint32_t> timeout = std::nullopt) const;
    [[nodiscard]] RarVerificationResult verifyWireAlternative(
        const aig::Circuit& golden, const WireAlternative& alt,
        std::optional<std::uint32_t> timeout = std::nullopt) const {
        return verify(golden, alt, timeout);
    }

private:
    const sat::SatBackend& backend_;
};

}  // namespace satrar::rar
