#pragma once

#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace satrar::cec {

enum class AbcVerdict { Equivalent, NotEquivalent, Undecided, Error, Missing };

[[nodiscard]] const char* abcVerdictName(AbcVerdict verdict) noexcept;

struct AbcScriptEntry {
    std::uint64_t id = 0;
    std::string left;
    std::string right;
};

[[nodiscard]] std::string buildAbcScript(const std::vector<AbcScriptEntry>& entries,
                                         std::uint32_t timeoutSeconds);

struct AbcJobLog {
    AbcVerdict verdict = AbcVerdict::Missing;
    /// i<k> values from an INPUT line. Missing names stay unset and replay fills them with 0.
    std::vector<std::optional<bool>> inputs;
    std::string excerpt;
};

struct AbcLogParse {
    std::unordered_map<std::uint64_t, AbcJobLog> jobs;
    bool malformed = false;
};

[[nodiscard]] AbcLogParse parseAbcLog(std::istream& log);

}  // namespace satrar::cec
