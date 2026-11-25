#pragma once

#include "satrar/analysis/FaultContext.hpp"
#include "satrar/rar/Alternative.hpp"

#include <functional>
#include <string>
#include <vector>

namespace satrar::rar {

enum class SatRarMode {
    TwoWayRarL0,
    TwoWayRarL1,
    TwoWayRarL2,
    SrarWire,
    SrarGate,
    SrarSubCircuit,
};

struct SatRarOptions {
    SatRarMode mode = SatRarMode::TwoWayRarL0;
    bool enableLearning = true;
    bool enableImplicationFilter = true;
    bool excludeTargetDirectReceiverGates = false;
    unsigned maxDecisionDepth = 2;
    std::size_t maxTrials = 20000;
    std::function<void(const std::string&)> trace;
};

struct RedundancyEvent {
    enum class Kind { Target, Dominator };
    Kind kind = Kind::Target;
    aig::NodeId node = aig::kInvalidNodeId;
};

struct SatRarStatistics {
    std::size_t trials = 0;
    std::size_t learned = 0;
    std::size_t learnedUnits = 0;
    std::vector<std::vector<std::uint32_t>> learnedClauses;
    std::size_t filtered = 0;
    bool limitReached = false;
    bool targetConflict = false;
    std::size_t redundantDominators = 0;
    std::vector<RedundancyEvent> redundancy;
    std::size_t subProbes = 0;
    std::size_t subReductions = 0;
    std::size_t subCandidates = 0;
    unsigned maxDepth = 0;
    std::string limitStep;
    std::string limitScope;
    double profileProjection = 0;
    double profileSetup = 0;
    double profileTrials = 0;
    double profileCanonical = 0;
    std::size_t incidentGateVisits = 0;
    std::size_t learnedClauseVisits = 0;
};

[[nodiscard]] std::string canonicalKey(const aig::Circuit& circuit, const Alternative& alternative);

class SatRarEngine {
public:
    explicit SatRarEngine(analysis::FaultContext& context) : context_(context) {}
    void setOptions(SatRarOptions options) { options_ = std::move(options); }
    void setPinTarget(analysis::PinFault fault) { target_ = fault; }
    [[nodiscard]] std::vector<Alternative> run();
    [[nodiscard]] const SatRarStatistics& statistics() const noexcept { return statistics_; }

private:
    analysis::FaultContext& context_;
    SatRarOptions options_;
    analysis::PinFault target_{};
    SatRarStatistics statistics_;
};

}  // namespace satrar::rar
