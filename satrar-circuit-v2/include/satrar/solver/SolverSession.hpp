#pragma once

#include "satrar/solver/GateNet.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace satrar::solver {

enum class QueryState { Open, Conflict, Limit };
enum class SolveState { Sat, Unsat, Unknown };

enum class ReasonKind : std::uint8_t { Decision, Gate, Learned, Root };

struct Budget {
    std::size_t eventLimit = static_cast<std::size_t>(-1);
    std::size_t decisionLimit = static_cast<std::size_t>(-1);
    std::size_t events = 0;
    std::size_t decisions = 0;
    bool chargeEvent();
    bool chargeDecision();
};

struct Checkpoint {
    std::uint32_t level = 0;
};

struct Support {
    std::vector<aig::Literal> roots;
};

struct ProbeResult {
    QueryState state = QueryState::Open;
    std::size_t trailBegin = 0;
};

struct SolveResult {
    SolveState state = SolveState::Unknown;
    std::vector<bool> primaryInputs;
    std::string reason;
    std::size_t decisions = 0;
    std::size_t conflicts = 0;
    std::size_t bcpVisits = 0;
    std::vector<std::vector<aig::Literal>> learned;
};

struct SessionStats {
    std::size_t incidentGateVisits = 0;
    std::size_t learnedClauseVisits = 0;
    std::size_t propagations = 0;
    std::size_t rootAssignments = 0;
    std::size_t learnedClauses = 0;
};

/// Gate-native BCP, guarded learning, and exact search over one GateNet.
class SolverSession {
public:
    explicit SolverSession(const GateNet& net);

    [[nodiscard]] const GateNet& net() const noexcept { return net_; }
    [[nodiscard]] const SessionStats& stats() const noexcept { return stats_; }
    [[nodiscard]] bool inconsistent() const noexcept { return inconsistent_; }
    [[nodiscard]] std::uint32_t contextLevel() const noexcept;
    [[nodiscard]] const std::vector<aig::Literal>& trail() const noexcept { return trailLits_; }
    [[nodiscard]] std::size_t learnedClauseCount() const noexcept { return clauses_.size(); }

    void setCdcl(bool enabled) noexcept { cdcl_ = enabled; }
    [[nodiscard]] bool cdcl() const noexcept { return cdcl_; }

    void forgetLearned();
    void newContextLevel();
    void cancelToContextLevel(std::uint32_t levelToKeep);

    [[nodiscard]] QueryState setAssumptions(const std::vector<aig::Literal>& ordered);
    [[nodiscard]] Checkpoint mark() const;
    void rollback(Checkpoint point);
    [[nodiscard]] ProbeResult probe(aig::Literal decision, Budget& budget);
    [[nodiscard]] bool pushAssumption(aig::Literal lit);
    [[nodiscard]] QueryState propagate(Budget* budget);

    [[nodiscard]] std::optional<bool> value(aig::Literal lit) const;
    [[nodiscard]] std::uint32_t level(aig::Literal lit) const;
    [[nodiscard]] bool isDecision(aig::Literal lit) const;
    [[nodiscard]] ReasonKind reasonKind(aig::Literal lit) const;
    /// Reason clause (disjunction) that implied this literal. Empty for a decision.
    [[nodiscard]] std::vector<aig::Literal> reasonClause(aig::Literal lit) const;

    [[nodiscard]] Support explain(aig::Literal implied) const;
    [[nodiscard]] Support explainConflict() const;
    [[nodiscard]] Support decisionSupport(std::optional<aig::Literal> implied) const;

    /// Install a circuit-valid clause and unit-propagate it on the current trail.
    [[nodiscard]] bool learnNow(std::vector<aig::Literal> clause);
    [[nodiscard]] const std::vector<std::vector<aig::Literal>>& learnedClauses() const noexcept {
        return clauses_;
    }

    [[nodiscard]] SolveResult solve(const std::vector<aig::Literal>& assumptions, Budget& budget);

private:
    struct Mark {
        std::size_t trailSize = 0;
        std::size_t qhead = 0;
        bool inconsistent = false;
    };
    struct Entry {
        aig::Literal lit;
        std::uint32_t level = 0;
        ReasonKind kind = ReasonKind::Root;
        std::uint32_t clauseId = 0;
        std::vector<aig::Literal> clause;
    };
    struct ConflictInfo {
        bool active = false;
        bool fromDecision = false;
        aig::Literal decision;
        std::vector<aig::Literal> clause;
    };

    const GateNet& net_;
    std::vector<std::int8_t> values_;
    std::vector<std::size_t> trailIndex_;
    std::vector<Entry> trail_;
    std::vector<aig::Literal> trailLits_;
    std::vector<Mark> marks_;
    std::vector<aig::Literal> assumptions_;
    std::vector<std::vector<aig::Literal>> clauses_;
    std::vector<std::vector<std::uint32_t>> clauseByVar_;
    ConflictInfo conflict_;
    SessionStats stats_;
    std::size_t qhead_ = 0;
    bool inconsistent_ = false;
    bool cdcl_ = false;
    bool constructing_ = false;

    void resetAssignments();
    void seedRoot();
    [[nodiscard]] bool enqueue(aig::Literal lit, ReasonKind kind, std::vector<aig::Literal> clause,
                               std::uint32_t clauseId);
    [[nodiscard]] bool propagateGate(std::uint32_t gate);
    [[nodiscard]] bool enforceClause(std::uint32_t clause);
    [[nodiscard]] bool addClause(std::vector<aig::Literal> clause);
    void clearConflict() { conflict_ = {}; }
    [[nodiscard]] std::vector<aig::Literal> analyze() const;
    [[nodiscard]] std::uint32_t backjumpLevel(const std::vector<aig::Literal>& clause) const;
    void walk(aig::Literal assignedTrue, std::vector<aig::Literal>& support,
              std::vector<char>& seen) const;
    [[nodiscard]] std::uint32_t denseOf(aig::Literal lit) const;
    [[nodiscard]] static bool sameLit(aig::Literal a, aig::Literal b) noexcept { return a == b; }
};

}  // namespace satrar::solver
