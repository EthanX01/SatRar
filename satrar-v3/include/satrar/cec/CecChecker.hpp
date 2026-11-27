#pragma once

#include "satrar/aig/Circuit.hpp"
#include "satrar/rar/EquivalenceChecker.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace satrar::cec {

struct CecBatchStats {
    std::uint64_t writeNs = 0;
    std::uint64_t abcNs = 0;
    /// Sum over jobs of the AND gates ABC actually reads (left plus right).
    std::uint64_t coneAnds = 0;
};

struct CecJob {
    std::uint64_t id = 0;
    /// Circuit form, used by single-shot commands and the reference builder.
    std::shared_ptr<const aig::Circuit> left;
    std::shared_ptr<const aig::Circuit> right;
    /// Pre-serialized binary AIGER. When both are set, ABC reads these bytes
    /// and a counterexample is replayed with replayDifference on the full circuits.
    std::shared_ptr<const std::string> leftAig;
    std::shared_ptr<const std::string> rightAig;
    std::uint32_t inputs = 0;
    std::uint32_t outputs = 0;
    std::uint32_t leftAnds = 0;
    std::uint32_t rightAnds = 0;
    /// nullopt means the full circuits match. Otherwise the first full-circuit output that differs.
    std::function<std::optional<std::size_t>(const std::vector<bool>&)> replayDifference;
};

/// Production code uses AbcCec. Tests supply fixed answers through this interface.
class CecChecker {
public:
    virtual ~CecChecker() = default;
    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::string describe() const = 0;
    /// One result per job, in job order. Per-job failures are results, not exceptions.
    [[nodiscard]] virtual std::vector<rar::EquivalenceResult> checkBatch(
        const std::vector<CecJob>& jobs, std::uint32_t timeoutSeconds,
        CecBatchStats* stats = nullptr) const = 0;
};

}  // namespace satrar::cec
