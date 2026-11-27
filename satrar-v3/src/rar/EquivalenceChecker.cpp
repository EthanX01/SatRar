#include "satrar/rar/EquivalenceChecker.hpp"

#include "satrar/cec/CecChecker.hpp"

#include <memory>

namespace satrar::rar {

const char* equivalenceVerdictName(EquivalenceVerdict verdict) noexcept {
    switch (verdict) {
        case EquivalenceVerdict::Equivalent: return "equivalent";
        case EquivalenceVerdict::NotEquivalent: return "not_equivalent";
        case EquivalenceVerdict::Unknown: return "unknown";
        case EquivalenceVerdict::Timeout: return "timeout";
        case EquivalenceVerdict::BackendError: return "backend_error";
        case EquivalenceVerdict::InvalidInput: return "invalid_input";
        case EquivalenceVerdict::Error: return "error";
    }
    return "error";
}

EquivalenceChecker::EquivalenceChecker(const cec::CecChecker& checker) : checker_(checker) {}

EquivalenceResult EquivalenceChecker::check(const aig::Circuit& golden, const aig::Circuit& candidate,
                                            std::optional<std::uint32_t> timeoutSeconds) const {
    cec::CecJob job;
    job.left = std::make_shared<aig::Circuit>(golden);
    job.right = std::make_shared<aig::Circuit>(candidate);
    const auto results = checker_.checkBatch({job}, timeoutSeconds.value_or(300));
    if (results.size() != 1) {
        EquivalenceResult result;
        result.verdict = EquivalenceVerdict::BackendError;
        result.message = "checker returned the wrong number of results";
        return result;
    }
    return results.front();
}

}  // namespace satrar::rar
