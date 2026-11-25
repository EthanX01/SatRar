
#pragma once

#include "satrar/analysis/FaultContext.hpp"
#include "satrar/rar/Alternative.hpp"

#include <functional>
#include <string>

namespace satrar::rar {

enum class SatRarMode {
    TwoWayRarL0, TwoWayRarL1, TwoWayRarL2, SrarWire, SrarGate, SrarSubCircuit,
};

struct SatRarOptions {
    SatRarMode mode = SatRarMode::TwoWayRarL0;
    bool enableLearning = true;
    bool enableImplicationFilter = true;
    bool excludeTargetDirectReceiverGates = false;
    unsigned maxDecisionDepth = 2;
    /// Shared budget. One unit is one decision trial: an outer trial, a recursive-learning split
    /// branch, a Theorem-3 operand probe or an operand-deletion retry.
    std::size_t maxTrials = 20000;
    /// Optional stable trace sink (one line per event, tab separated). Empty by default.
    std::function<void(const std::string&)> trace;
};

/// Figure 5 lines 4-5 / 9-10: a fault context that has no detecting assignment.
struct RedundancyEvent {
    enum class Kind { Target, Dominator };
    Kind kind = Kind::Target;
    aig::NodeId node = aig::kInvalidNodeId;  ///< Receiving gate (target) or dominator gate.
};

struct SatRarStatistics {
    std::size_t trials = 0;
    std::size_t learned = 0;          ///< Distinct circuit-valid clauses installed during the run.
    std::size_t learnedUnits = 0;     ///< New assignments produced at once by installed clauses.
    /// The installed clauses (DIMACS over FaultContext::cnf()); each is a circuit entailment.
    std::vector<std::vector<sat::DimacsLiteral>> learnedClauses;
    std::size_t filtered = 0;
    bool limitReached = false;
    bool targetConflict = false;      ///< Target fault is redundant (redundant_target event).
    std::size_t redundantDominators = 0;
    std::vector<RedundancyEvent> redundancy;
    std::size_t subProbes = 0;        ///< Theorem-3 target-side refutation probes.
    std::size_t subReductions = 0;    ///< Operands removed while shrinking B.
    std::size_t subCandidates = 0;
    unsigned maxDepth = 0;            ///< Deepest decision stack that was tried.
    std::string limitStep;            ///< trial | split | sub_probe | sub_shrink once limited.
    std::string limitScope;           ///< target or dominator:<aig var> where the limit was hit.
    // Profiling (seconds). Trials include candidate canonicalization and Theorem-3 probes.
    double profileProjection = 0;     ///< Reconvergence projection inside mandatory-assignment setup.
    double profileSetup = 0;          ///< Assumption stack + BCP of target / dominator contexts.
    double profileTrials = 0;         ///< Decision-trial expansion (explore) over all dominators.
    double profileCanonical = 0;      ///< Candidate canonicalization and de-duplication.
};

class SatRarEngine {
public:
    explicit SatRarEngine(analysis::FaultContext& context) : context_(context) {}
    void setOptions(SatRarOptions options) { options_ = options; }
    void setPinTarget(analysis::PinFault fault) { target_ = fault; }
    [[nodiscard]] std::vector<Alternative> run();
    [[nodiscard]] const SatRarStatistics& statistics() const noexcept { return statistics_; }

private:
    using Split = std::pair<sat::DimacsLiteral, std::pair<sat::DimacsLiteral, sat::DimacsLiteral>>;
    analysis::FaultContext& context_;
    SatRarOptions options_;
    analysis::PinFault target_{};
    SatRarStatistics statistics_;
    std::vector<Split> splits_;
    std::vector<aig::Literal> signals_;
};

}  // namespace satrar::rar
