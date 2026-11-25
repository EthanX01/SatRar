#pragma once

#include "satrar/sat/DimacsLiteral.hpp"

#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace satrar::sat {

class CnfFormula {
public:
    void clear() {
        clauses_.clear();
        numVars_ = 0;
        aigVarToSat_.clear();
    }

    [[nodiscard]] std::uint32_t numVariables() const noexcept { return numVars_; }
    [[nodiscard]] const std::vector<std::vector<DimacsLiteral>>& clauses() const noexcept { return clauses_; }

    std::uint32_t newVariable() {
        if (numVars_ == static_cast<std::uint32_t>(std::numeric_limits<DimacsLiteral>::max())) {
            throw std::overflow_error("too many DIMACS variables");
        }
        return ++numVars_;
    }

    std::uint32_t ensureAigVar(std::uint32_t aigVariable) {
        const auto it = aigVarToSat_.find(aigVariable);
        if (it != aigVarToSat_.end()) {
            return it->second;
        }
        const std::uint32_t satVar = newVariable();
        aigVarToSat_[aigVariable] = satVar;
        return satVar;
    }

    [[nodiscard]] std::uint32_t satVarOfAig(std::uint32_t aigVariable) const {
        const auto it = aigVarToSat_.find(aigVariable);
        if (it == aigVarToSat_.end()) {
            throw std::out_of_range("AIG variable not encoded");
        }
        return it->second;
    }

    void addClause(std::vector<DimacsLiteral> clause) { clauses_.push_back(std::move(clause)); }

    void writeDimacs(std::ostream& out) const {
        out << "p cnf " << numVars_ << ' ' << clauses_.size() << '\n';
        for (const auto& clause : clauses_) {
            for (const DimacsLiteral lit : clause) {
                out << lit << ' ';
            }
            out << "0\n";
        }
    }

    [[nodiscard]] const std::unordered_map<std::uint32_t, std::uint32_t>& aigToSat() const noexcept {
        return aigVarToSat_;
    }

private:
    std::uint32_t numVars_ = 0;
    std::vector<std::vector<DimacsLiteral>> clauses_;
    std::unordered_map<std::uint32_t, std::uint32_t> aigVarToSat_;
};

}  // namespace satrar::sat
