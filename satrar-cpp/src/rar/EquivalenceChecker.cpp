
#include "satrar/rar/EquivalenceChecker.hpp"

#include "satrar/sat/MiterBuilder.hpp"
#include "satrar/sat/TempDirectory.hpp"

#include <cstdlib>
#include <fstream>
#include <limits>
#include <unordered_map>

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

EquivalenceChecker::EquivalenceChecker(const sat::SatBackend& backend) : backend_(backend) {}

EquivalenceResult EquivalenceChecker::check(const aig::Circuit& golden, const aig::Circuit& candidate,
                                             std::optional<std::uint32_t> timeout) const {
    EquivalenceResult result;
    sat::CnfFormula miter;
    try {
        miter = sat::MiterBuilder::buildNotEquivalentFormula(golden, candidate);
    } catch (const std::exception& ex) {
        result.verdict = EquivalenceVerdict::InvalidInput;
        result.message = ex.what();
        return result;
    }
    try {
        if (!backend_.available()) {
            throw std::runtime_error("verification backend unavailable");
        }
        const sat::TempDirectory temporary;
        const auto path = temporary.path() / "miter.cnf";
        {
            std::ofstream output(path);
            miter.writeDimacs(output);
            output.close();
            if (!output) {
                throw std::runtime_error("cannot write miter CNF");
            }
        }
        const auto answer = backend_.solveDimacsFile(path, timeout);
        result.elapsedSeconds = answer.elapsedSeconds;
        result.message = answer.message;
        switch (answer.verdict) {
            case sat::SatVerdict::Unsat:
                result.verdict = EquivalenceVerdict::Equivalent;
                return result;
            case sat::SatVerdict::Unknown:
                result.verdict = EquivalenceVerdict::Unknown;
                return result;
            case sat::SatVerdict::Timeout:
                result.verdict = EquivalenceVerdict::Timeout;
                return result;
            case sat::SatVerdict::Error:
                result.verdict = EquivalenceVerdict::BackendError;
                return result;
            case sat::SatVerdict::Sat:
                break;
        }
        std::unordered_map<std::uint32_t, bool> values;
        for (const auto lit : answer.modelLits) {
            if (lit == 0 || lit == std::numeric_limits<sat::DimacsLiteral>::min() ||
                sat::dimacsVariable(lit) > miter.numVariables()) {
                throw std::runtime_error("invalid literal in SAT model");
            }
            const auto [it, inserted] = values.emplace(sat::dimacsVariable(lit), lit > 0);
            if (!inserted && it->second != (lit > 0)) {
                throw std::runtime_error("conflicting values in SAT model");
            }
        }
        for (std::size_t i = 0; i < golden.primaryInputIds().size(); ++i) {
            const auto var = miter.satVarOfAig(sat::MiterBuilder::primaryInputEncodingKey(i));
            const auto found = values.find(var);
            if (found == values.end()) {
                throw std::runtime_error("SAT model is missing a primary input");
            }
            result.counterexamplePi.push_back(found->second);
        }
        const auto a = golden.evaluate(result.counterexamplePi);
        const auto b = candidate.evaluate(result.counterexamplePi);
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] != b[i]) {
                result.differingOutput = i;
                result.counterexampleReplayOk = true;
                result.verdict = EquivalenceVerdict::NotEquivalent;
                return result;
            }
        }
        throw std::runtime_error("SAT counterexample did not replay");
    } catch (const std::exception& ex) {
        result.verdict = EquivalenceVerdict::BackendError;
        result.message = ex.what();
        return result;
    }
}

}  // namespace satrar::rar
