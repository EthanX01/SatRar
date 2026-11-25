#pragma once

#include "satrar/aig/Circuit.hpp"
#include "satrar/sat/SatBackend.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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
    /// PI assignment if NotEquivalent and solver returned a model.
    std::vector<bool> counterexamplePi;
    bool counterexampleReplayOk = false;
    std::optional<std::size_t> differingOutput;
};

/// Miter + external SAT: UNSAT => equivalent, SAT => diff (with optional simulation replay).
class EquivalenceChecker {
public:
    explicit EquivalenceChecker(const satrar::sat::SatBackend& backend);

    [[nodiscard]] EquivalenceResult check(const aig::Circuit& golden, const aig::Circuit& candidate,
                                       std::optional<std::uint32_t> timeoutSeconds =
                                           std::nullopt) const;

private:
    const satrar::sat::SatBackend& backend_;
};

}  // namespace satrar::rar
