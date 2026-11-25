
#pragma once

#include "satrar/analysis/CircuitPropagator.hpp"
#include "satrar/analysis/Fault.hpp"
#include "satrar/analysis/GraphAnalysis.hpp"
#include "satrar/analysis/MandatoryAssignments.hpp"

#include <variant>

namespace satrar::analysis {

enum class FaultStatus { Consistent, Conflict, NeedsExactTest, NoFault };

/// Time spent in the two expensive parts of a mandatory-assignment computation (profiling only).
struct FaultProfile {
    double projectionSeconds = 0.0;   ///< Exact reconvergence projection (separate miter + BCP).
    double propagationSeconds = 0.0;  ///< Assumption stack rebuild and BCP on the shared propagator.
    std::size_t projections = 0;
    std::size_t computations = 0;
};

class FaultContext {
public:
    explicit FaultContext(aig::Circuit circuit);
    FaultContext(const FaultContext&) = delete;
    FaultContext& operator=(const FaultContext&) = delete;

    void resetSolver();
    void rollbackFaultContext();
    void setPinFault(PinFault fault);
    void setDominatorOutputFault(DominatorOutputFault fault);
    void clearFault();

    [[nodiscard]] const aig::Circuit& circuit() const noexcept { return circuit_; }
    [[nodiscard]] const GraphAnalysis& graph() const noexcept { return graph_; }
    [[nodiscard]] const sat::CnfFormula& cnf() const noexcept { return propagator_.cnf(); }
    [[nodiscard]] CircuitPropagator& propagator() noexcept { return propagator_; }
    [[nodiscard]] FaultStatus status() const noexcept { return status_; }
    [[nodiscard]] const FaultProfile& profile() const noexcept { return profile_; }
    void resetProfile() noexcept { profile_ = {}; }
    [[nodiscard]] const std::vector<sat::DimacsLiteral>& requirements() const noexcept { return requirements_; }

    [[nodiscard]] MandatoryAssignments computeMandatoryAssignments(
        const std::vector<sat::DimacsLiteral>& extraAssumptions = {});
    [[nodiscard]] MandatoryAssignments snapshot() const;
    [[nodiscard]] std::optional<bool> impliedValue(sat::DimacsLiteral lit) const;
    [[nodiscard]] std::optional<sat::DimacsLiteral> stuckAtAssumption() const;
    [[nodiscard]] sat::CnfFormula faultDetectionFormula(
        const std::vector<sat::DimacsLiteral>& extraAssumptions = {}) const;

private:
    aig::Circuit circuit_;
    GraphAnalysis graph_;
    CircuitPropagator propagator_;
    std::variant<std::monostate, PinFault, DominatorOutputFault> fault_;
    std::vector<sat::DimacsLiteral> requirements_;
    FaultStatus status_ = FaultStatus::NoFault;
    FaultProfile profile_;

    [[nodiscard]] aig::Circuit faultyCircuit() const;
    bool projectFaultImplications(aig::NodeId start,
                                  const std::unordered_set<aig::NodeId>& affected);
};

}  // namespace satrar::analysis
