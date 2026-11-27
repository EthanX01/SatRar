#pragma once

#include "satrar/aig/Circuit.hpp"
#include "satrar/rar/EquivalenceChecker.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace satrar::cec {
class CecChecker;
}

namespace satrar::rar {

struct ExportOptions {
    std::optional<std::uint32_t> timeoutSeconds;
    /// Called after the temporary file passed its round-trip CEC and before it replaces
    /// the destination. Throwing aborts the publish.
    std::function<void(const std::filesystem::path& temporary)> beforePublish;
};

struct ExportResult {
    bool published = false;
    EquivalenceVerdict roundTrip = EquivalenceVerdict::Error;
    std::string message;
};

/// Write \p rewritten beside \p destination, re-read it, prove it equivalent with ABC,
/// then replace \p destination. On failure the previous destination is left in place.
[[nodiscard]] ExportResult exportVerifiedAag(const aig::Circuit& original,
                                              const aig::Circuit& rewritten,
                                              const std::filesystem::path& destination,
                                              const cec::CecChecker& checker,
                                              const ExportOptions& options = {});

}  // namespace satrar::rar
