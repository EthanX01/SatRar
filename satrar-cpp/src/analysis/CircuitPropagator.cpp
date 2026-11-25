
#include "satrar/analysis/CircuitPropagator.hpp"

#include "satrar/sat/CnfEncoder.hpp"

#include <algorithm>
#include <unordered_set>

namespace satrar::analysis {

using sat::DimacsLiteral;
using sat::dimacsVariable;

CircuitPropagator::CircuitPropagator(const aig::Circuit& circuit)
    : cnf_(sat::CnfEncoder::encodeCircuit(circuit)), values_(cnf_.numVariables() + 1),
      watchers_(values_.size()) {
    // Each gate registers its forward and backward implications on the incident signals.
    const auto literal = [&](aig::Literal edge) {
        return sat::CnfEncoder::dimacsLitForAig(cnf_, edge).value();
    };
    addRule({-literal(aig::Literal(0))});
    addRule({literal(aig::Literal(1))});
    for (const auto& node : circuit.nodes()) {
        if (node.isAnd()) {
            const auto z = literal(aig::Literal::fromVariable(node.aagVar));
            const auto x = literal(node.andInputs().left);
            const auto y = literal(node.andInputs().right);
            addRule({-z, x});
            addRule({-z, y});
            addRule({z, -x, -y});
        }
    }
    baseRules_ = rules_.size();
    reset();
}

CircuitPropagator::CircuitPropagator(sat::CnfFormula formula)
    : cnf_(std::move(formula)), values_(cnf_.numVariables() + 1), watchers_(values_.size()) {
    for (const auto& clause : cnf_.clauses()) {
        addRule(clause);
    }
    baseRules_ = rules_.size();
    reset();
}

void CircuitPropagator::forgetLearned() {
    if (rules_.size() == baseRules_) {
        setAssumptions({});
        return;
    }
    while (rules_.size() > baseRules_) {
        for (const auto lit : rules_.back()) {
            watchers_[dimacsVariable(lit)].pop_back();
        }
        if (rules_.back().size() <= 1) {
            units_.pop_back();
        }
        rules_.pop_back();
    }
    reset();
}

void CircuitPropagator::addRule(std::vector<DimacsLiteral> rule) {
    std::sort(rule.begin(), rule.end());
    rule.erase(std::unique(rule.begin(), rule.end()), rule.end());
    for (auto lit : rule) {
        if (std::binary_search(rule.begin(), rule.end(), -lit)) {
            return;
        }
    }
    const auto id = rules_.size();
    for (auto lit : rule) {
        watchers_.at(dimacsVariable(lit)).push_back(id);
    }
    if (rule.size() <= 1) {
        units_.push_back(id);
    }
    rules_.push_back(std::move(rule));
}

void CircuitPropagator::reset() {
    std::fill(values_.begin(), values_.end(), Assignment{});
    trail_.clear();
    assumptions_.clear();
    marks_.assign(1, Mark{0, 0, 0, 0, false});
    pending_ = 0;
    inconsistent_ = false;
    conflictRule_ = 0;
    conflictAssumption_ = 0;
    for (const auto rule : units_) {
        if (!enforce(rule)) {
            break;
        }
    }
    propagate();
    newContextLevel();
}

std::uint32_t CircuitPropagator::contextLevel() const noexcept {
    return static_cast<std::uint32_t>(marks_.size() - 1);
}

void CircuitPropagator::newContextLevel() {
    marks_.push_back({trail_.size(), pending_, conflictRule_, conflictAssumption_, inconsistent_});
}

void CircuitPropagator::cancelToContextLevel(std::uint32_t levelToKeep) {
    if (levelToKeep >= contextLevel()) {
        return;
    }
    const auto mark = marks_[levelToKeep + 1];
    // A learned clause may have fired late (its premises sit on lower levels than the level at
    // which it was installed). When backtracking removes such an implication the clause must be
    // checked again, otherwise the closure would be weaker than a cold start.
    std::vector<std::size_t> lostLearned;
    while (trail_.size() > mark.trailSize) {
        auto& assignment = values_[dimacsVariable(trail_.back())];
        if (assignment.reason > baseRules_) {
            lostLearned.push_back(assignment.reason - 1);
        }
        assignment = {};
        trail_.pop_back();
    }
    marks_.resize(levelToKeep + 1);
    pending_ = mark.pending;
    inconsistent_ = mark.inconsistent;
    conflictRule_ = mark.conflictRule;
    conflictAssumption_ = mark.conflictAssumption;
    if (assumptions_.size() > levelToKeep) {
        assumptions_.resize(levelToKeep);
    }
    for (const auto rule : lostLearned) {
        if (inconsistent_) {
            break;
        }
        (void)enforce(rule);
    }
    if (!lostLearned.empty()) {
        (void)propagate();
    }
}

bool CircuitPropagator::learnNow(std::vector<DimacsLiteral> clause) {
    const auto before = rules_.size();
    addRule(std::move(clause));
    if (rules_.size() == before) {
        return !inconsistent_;  // tautology: nothing to install
    }
    if (!inconsistent_) {
        (void)enforce(rules_.size() - 1);
    }
    return propagate();
}

void CircuitPropagator::setAssumptions(const std::vector<DimacsLiteral>& assumptions) {
    std::size_t common = 0;
    while (common < assumptions_.size() && common < assumptions.size() &&
           assumptions_[common] == assumptions[common]) {
        ++common;
    }
    cancelToContextLevel(static_cast<std::uint32_t>(common));
    assumptions_.resize(common);
    for (auto i = common; i < assumptions.size(); ++i) {
        newContextLevel();
        assumptions_.push_back(assumptions[i]);
        if (!pushAssumption(assumptions[i])) {
            break;
        }
    }
}

std::optional<bool> CircuitPropagator::value(DimacsLiteral lit) const {
    const auto state = values_.at(dimacsVariable(lit)).value;
    if (state == 0) {
        return std::nullopt;
    }
    return (state > 0) == (lit > 0);
}

std::uint32_t CircuitPropagator::level(DimacsLiteral lit) const {
    return values_.at(dimacsVariable(lit)).level;
}

const std::vector<DimacsLiteral>& CircuitPropagator::reason(DimacsLiteral lit) const {
    static const std::vector<DimacsLiteral> empty;
    const auto rule = values_.at(dimacsVariable(lit)).reason;
    return rule ? rules_[rule - 1] : empty;
}

bool CircuitPropagator::assign(DimacsLiteral lit, std::size_t rule) {
    if (const auto previous = value(lit)) {
        return *previous;
    }
    values_[dimacsVariable(lit)] = {static_cast<std::int8_t>(lit > 0 ? 1 : -1),
                                   contextLevel(), rule};
    trail_.push_back(lit);
    return true;
}

bool CircuitPropagator::enforce(std::size_t rule) {
    DimacsLiteral unit = 0;
    for (const auto lit : rules_[rule]) {
        const auto state = value(lit);
        if (state && *state) {
            return true;
        }
        if (!state) {
            if (unit) {
                return true;
            }
            unit = lit;
        }
    }
    if (unit) {
        return assign(unit, rule + 1);
    }
    conflictRule_ = rule + 1;
    inconsistent_ = true;
    return false;
}

bool CircuitPropagator::pushAssumption(DimacsLiteral lit) {
    if (!inconsistent_ && !assign(lit, 0)) {
        conflictAssumption_ = lit;
        inconsistent_ = true;
    }
    return propagate();
}

bool CircuitPropagator::propagate(std::vector<DimacsLiteral>* out) {
    while (!inconsistent_ && pending_ < trail_.size()) {
        for (const auto rule : watchers_[dimacsVariable(trail_[pending_++])]) {
            if (!enforce(rule)) {
                break;
            }
        }
    }
    if (out) {
        *out = inconsistent_ ? std::vector<DimacsLiteral>{} : trail_;
    }
    return !inconsistent_;
}

std::vector<DimacsLiteral> CircuitPropagator::decisionSupport(DimacsLiteral implied) const {
    std::vector<DimacsLiteral> support;
    std::vector<DimacsLiteral> todo;
    if (implied) {
        todo.push_back(implied);
    } else if (conflictRule_) {
        for (auto literal : rules_[conflictRule_ - 1]) {
            todo.push_back(-literal);
        }
    } else if (conflictAssumption_) {
        support.push_back(conflictAssumption_);
        todo.push_back(-conflictAssumption_);
    }
    std::unordered_set<std::uint32_t> visited;
    while (!todo.empty()) {
        const auto lit = todo.back();
        todo.pop_back();
        const auto var = dimacsVariable(lit);
        if (!visited.insert(var).second) {
            continue;
        }
        const auto& why = reason(lit);
        if (why.empty()) {
            support.push_back(lit);
        } else {
            for (auto antecedent : why) {
                if (dimacsVariable(antecedent) != var) {
                    todo.push_back(-antecedent);
                }
            }
        }
    }
    return support;
}

void CircuitPropagator::learn(std::vector<DimacsLiteral> clause) {
    addRule(std::move(clause));
    reset();
}

void CircuitPropagator::learnAll(const std::set<std::vector<DimacsLiteral>>& clauses) {
    if (clauses.empty()) {
        return;
    }
    for (const auto& clause : clauses) {
        addRule(clause);
    }
    reset();
}

}  // namespace satrar::analysis
