#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace satrar::sat {

enum class SatVerdict {
    Sat,
    Unsat,
    Unknown,
    Timeout,
    Error,
};

struct SatSolveResult {
    SatVerdict verdict = SatVerdict::Error;
    int exitCode = -1;
    std::string message;
    double elapsedSeconds = 0.0;
    std::vector<std::int32_t> modelLits;
};

/// Full CDCL solve on a DIMACS file (verification backend — not propagation).
class SatBackend {
public:
    virtual ~SatBackend() = default;

    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::string describe() const = 0;

    [[nodiscard]] virtual SatSolveResult solveDimacsFile(
        const std::filesystem::path& cnfPath,
        std::optional<std::uint32_t> timeoutSeconds = std::nullopt) const = 0;
};

[[nodiscard]] const char* satVerdictName(SatVerdict v) noexcept;

}  // namespace satrar::sat
