#pragma once

#include "satrar/aig/Circuit.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace satrar::cec {
class CecChecker;
}

namespace satrar::rar {

enum class EquivalenceVerdict {
    Equivalent,
    NotEquivalent,
    Unknown,
    Timeout,
    BackendError,
    InvalidInput,
    Error,
};

[[nodiscard]] const char* equivalenceVerdictName(EquivalenceVerdict verdict) noexcept;

struct EquivalenceResult {
    EquivalenceVerdict verdict = EquivalenceVerdict::Error;
    std::string message;
    double elapsedSeconds = 0.0;
    /// Primary-input assignment when NotEquivalent and the counterexample replayed.
    std::vector<bool> counterexamplePi;
    bool counterexampleReplayOk = false;
    std::optional<std::size_t> differingOutput;
};

/// One ABC equivalence job between two combinational circuits.
class EquivalenceChecker {
public:
    explicit EquivalenceChecker(const cec::CecChecker& checker);

    [[nodiscard]] EquivalenceResult check(const aig::Circuit& golden, const aig::Circuit& candidate,
                                          std::optional<std::uint32_t> timeoutSeconds =
                                              std::nullopt) const;

private:
    const cec::CecChecker& checker_;
};

}  // namespace satrar::rar
