
#pragma once

#include "satrar/aig/Circuit.hpp"
#include "satrar/sat/CnfFormula.hpp"

#include <optional>
#include <set>

namespace satrar::analysis {

class CircuitPropagator {
public:
    explicit CircuitPropagator(const aig::Circuit& circuit);
    explicit CircuitPropagator(sat::CnfFormula formula);

    void reset();
    void forgetLearned();
    [[nodiscard]] std::uint32_t contextLevel() const noexcept;
    void newContextLevel();
    void cancelToContextLevel(std::uint32_t level);
    void setAssumptions(const std::vector<sat::DimacsLiteral>& assumptions);
    bool pushAssumption(sat::DimacsLiteral lit);
    bool propagate(std::vector<sat::DimacsLiteral>* out = nullptr);
    void learn(std::vector<sat::DimacsLiteral> clause);
    void learnAll(const std::set<std::vector<sat::DimacsLiteral>>& clauses);
    /// Install a circuit-valid clause without discarding the trail: the rule is checked against
    /// the current assignments (it may become unit or conflicting at the current level) and then
    /// propagated. Backtracking re-checks every learned clause whose implication it removes,
    /// so a clause that fired late never makes the closure weaker than a cold start. Returns false when the current context becomes inconsistent.
    bool learnNow(std::vector<sat::DimacsLiteral> clause);
    [[nodiscard]] std::size_t learnedRuleCount() const noexcept { return rules_.size() - baseRules_; }
    [[nodiscard]] bool inconsistent() const noexcept { return inconsistent_; }

    [[nodiscard]] std::optional<bool> value(sat::DimacsLiteral lit) const;
    [[nodiscard]] std::uint32_t level(sat::DimacsLiteral lit) const;
    [[nodiscard]] const std::vector<sat::DimacsLiteral>& reason(sat::DimacsLiteral lit) const;
    [[nodiscard]] std::vector<sat::DimacsLiteral> decisionSupport(sat::DimacsLiteral implied = 0) const;
    [[nodiscard]] const std::vector<sat::DimacsLiteral>& trail() const noexcept { return trail_; }
    [[nodiscard]] const sat::CnfFormula& cnf() const noexcept { return cnf_; }

private:
    struct Assignment {
        std::int8_t value = 0;
        std::uint32_t level = 0;
        std::size_t reason = 0; // Rule index + 1, zero for a decision.
    };
    struct Mark {
        std::size_t trailSize;
        std::size_t pending;
        std::size_t conflictRule;
        sat::DimacsLiteral conflictAssumption;
        bool inconsistent;
    };

    sat::CnfFormula cnf_;
    std::vector<Assignment> values_;
    std::vector<std::vector<sat::DimacsLiteral>> rules_;
    std::vector<std::vector<std::size_t>> watchers_;
    std::vector<std::size_t> units_;
    std::vector<sat::DimacsLiteral> trail_;
    std::vector<sat::DimacsLiteral> assumptions_;
    std::vector<Mark> marks_;
    std::size_t pending_ = 0;
    std::size_t baseRules_ = 0;
    std::size_t conflictRule_ = 0;
    sat::DimacsLiteral conflictAssumption_ = 0;
    bool inconsistent_ = false;

    void addRule(std::vector<sat::DimacsLiteral> rule);
    bool assign(sat::DimacsLiteral lit, std::size_t rule);
    bool enforce(std::size_t rule);
};

}  // namespace satrar::analysis
