#pragma once

#include "satrar/sat/SatBackend.hpp"

#include <filesystem>
#include <string>

namespace satrar::sat {

/// Spawns external `cadical` (exit 10=SAT, 20=UNSAT). Optional — unconfigured => `available()==false`.
class CadicalBackend final : public SatBackend {
public:
    CadicalBackend();
    explicit CadicalBackend(std::filesystem::path executable);

    void setExecutable(std::filesystem::path executable);
    [[nodiscard]] const std::filesystem::path& executable() const noexcept { return executable_; }

    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string describe() const override;

    [[nodiscard]] SatSolveResult solveDimacsFile(
        const std::filesystem::path& cnfPath,
        std::optional<std::uint32_t> timeoutSeconds = std::nullopt) const override;

private:
    std::filesystem::path executable_;
};

[[nodiscard]] std::unique_ptr<SatBackend> defaultVerificationBackend();

}  // namespace satrar::sat