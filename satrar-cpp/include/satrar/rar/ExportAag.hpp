
#pragma once

#include "satrar/aig/Circuit.hpp"
#include "satrar/rar/EquivalenceChecker.hpp"
#include "satrar/sat/SatBackend.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace satrar::rar {

struct ExportOptions {
    std::optional<std::uint32_t> timeoutSeconds;
    /// Fault-injection / audit hook called after the temporary file passed its round-trip CEC
    /// and before it replaces the destination. Throwing aborts the publish.
    std::function<void(const std::filesystem::path& temporary)> beforePublish;
};

struct ExportResult {
    bool published = false;
    EquivalenceVerdict roundTrip = EquivalenceVerdict::Error;
    std::string message;
};

/// Write \p rewritten next to \p destination, re-read it from disk, prove it equivalent to
/// \p original, then atomically replace \p destination. On any failure the previous destination
/// is untouched and the temporary file is removed.
[[nodiscard]] ExportResult exportVerifiedAag(const aig::Circuit& original,
                                              const aig::Circuit& rewritten,
                                              const std::filesystem::path& destination,
                                              const sat::SatBackend& backend,
                                              const ExportOptions& options = {});

}  // namespace satrar::rar
