#pragma once

#include "satrar/rar/Alternative.hpp"
#include "satrar/rar/EquivalenceChecker.hpp"

namespace satrar::cec {
class CecChecker;
}

namespace satrar::rar {

enum class RarAcceptance { Accepted, Rejected, Unverified };
enum class RarStage { None, Addition, Removal, EndToEnd };

[[nodiscard]] const char* rarAcceptanceName(RarAcceptance acceptance) noexcept;
[[nodiscard]] const char* rarStageName(RarStage stage) noexcept;

struct RarVerificationResult {
    RarAcceptance acceptance = RarAcceptance::Unverified;
    RarStage failedStage = RarStage::None;
    EquivalenceResult addition;
    std::optional<EquivalenceResult> removal;
    std::optional<aig::Circuit> rewritten;
    std::string message;
    double verifySeconds = 0.0;
};

/// Two-stage check: N0 == N1 (alternative added) and N1 == N2 (target removed).
class RarVerifier {
public:
    explicit RarVerifier(const cec::CecChecker& checker) : checker_(checker) {}

    [[nodiscard]] RarVerificationResult verify(const aig::Circuit& golden, const Alternative& alt,
                                               std::optional<std::uint32_t> timeout = std::nullopt) const;
    [[nodiscard]] RarVerificationResult verifyWireAlternative(
        const aig::Circuit& golden, const WireAlternative& alt,
        std::optional<std::uint32_t> timeout = std::nullopt) const {
        return verify(golden, alt, timeout);
    }

private:
    const cec::CecChecker& checker_;
};

}  // namespace satrar::rar
