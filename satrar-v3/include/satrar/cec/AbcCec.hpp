#pragma once

#include "satrar/cec/CecChecker.hpp"

#include <filesystem>

namespace satrar::cec {

struct AbcOptions {
    std::filesystem::path executable;
    std::uint32_t batchTimeoutCap = 1800;
    bool keepFiles = false;
};

[[nodiscard]] std::filesystem::path defaultAbcExecutable();

class AbcCec final : public CecChecker {
public:
    explicit AbcCec(AbcOptions options = {});
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string describe() const override;
    [[nodiscard]] std::vector<rar::EquivalenceResult> checkBatch(
        const std::vector<CecJob>& jobs, std::uint32_t timeoutSeconds,
        CecBatchStats* stats = nullptr) const override;

private:
    [[nodiscard]] std::vector<rar::EquivalenceResult> runOnce(const std::vector<CecJob>& jobs,
                                                              std::uint32_t timeoutSeconds,
                                                              CecBatchStats* stats) const;
    AbcOptions options_;
};

}  // namespace satrar::cec
