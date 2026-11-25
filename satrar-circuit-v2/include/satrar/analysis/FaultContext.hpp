#pragma once

#include "satrar/analysis/Fault.hpp"
#include "satrar/analysis/GraphAnalysis.hpp"
#include "satrar/analysis/MandatoryAssignments.hpp"
#include "satrar/solver/GateNet.hpp"
#include "satrar/solver/SolverSession.hpp"

#include <unordered_map>
#include <variant>

namespace satrar::analysis {

enum class FaultStatus { Consistent, Conflict, NeedsExactTest, NoFault };

struct FaultProfile {
    double projectionSeconds = 0;
    double propagationSeconds = 0;
    std::size_t projections = 0;
    std::size_t computations = 0;
};

struct ExactAnswer {
    solver::SolveState state = solver::SolveState::Unknown;
    std::vector<bool> primaryInputs;
    std::string reason;
    bool redundantFault = false;
    bool mandatory = false;
};

/// Gate-native fault context. Good-circuit BCP runs on the shared session.
/// Reconvergent faults build a separate good/faulty miter and project D_f=1.
class FaultContext {
public:
    explicit FaultContext(aig::Circuit circuit);

    void resetSolver();
    void setPinFault(PinFault fault);
    void setDominatorOutputFault(DominatorOutputFault fault);
    void clearFault();

    [[nodiscard]] const aig::Circuit& circuit() const noexcept { return circuit_; }
    [[nodiscard]] const GraphAnalysis& graph() const noexcept { return graph_; }
    [[nodiscard]] const solver::GateNet& net() const noexcept { return net_; }
    [[nodiscard]] solver::SolverSession& session() noexcept { return session_; }
    [[nodiscard]] FaultStatus status() const noexcept { return status_; }
    [[nodiscard]] const FaultProfile& profile() const noexcept { return profile_; }
    void resetProfile() noexcept { profile_ = {}; }
    [[nodiscard]] const std::vector<aig::Literal>& requirements() const noexcept { return requirements_; }

    [[nodiscard]] MandatoryAssignments computeMandatoryAssignments(
        const std::vector<aig::Literal>& extra = {});
    [[nodiscard]] MandatoryAssignments snapshot() const;

    /// Exact detection on the native miter. Extra literals are good-circuit assumptions.
    [[nodiscard]] ExactAnswer solveDetection(const std::vector<aig::Literal>& extra, solver::Budget& budget,
                                             bool cdcl) const;
    /// UNSAT(D_f) is a redundant fault. UNSAT(D_f and not lit) means lit is mandatory.
    [[nodiscard]] ExactAnswer checkMandatory(aig::Literal lit, solver::Budget& budget, bool cdcl) const;

private:
    struct Miter {
        aig::Circuit circuit;
        aig::Literal detect = aig::Literal(0);
        std::unordered_map<std::uint32_t, aig::Literal> goodOf;
        bool observable = false;
    };

    aig::Circuit circuit_;
    GraphAnalysis graph_;
    solver::GateNet net_;
    solver::SolverSession session_;
    std::variant<std::monostate, PinFault, DominatorOutputFault> fault_;
    std::vector<aig::Literal> requirements_;
    FaultStatus status_ = FaultStatus::NoFault;
    FaultProfile profile_;

    [[nodiscard]] std::optional<aig::Literal> excitation() const;
    [[nodiscard]] Miter buildMiter() const;
    [[nodiscard]] bool project(const Miter& miter);
};

}  // namespace satrar::analysis
