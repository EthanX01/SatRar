#include "satrar/rar/SatRarEngine.hpp"

#include "satrar/solver/SolverSession.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <unordered_set>

namespace satrar::rar {
namespace {

using aig::Literal;
using solver::SolverSession;

bool consumeTrial(const SatRarOptions& options, SatRarStatistics& stats, const char* step) {
    if (stats.trials >= options.maxTrials) {
        if (!stats.limitReached) {
            stats.limitReached = true;
            stats.limitStep = step;
        }
        return false;
    }
    ++stats.trials;
    return true;
}

enum class Rec { Ok, Conflict, Limited };

Rec recursiveLearn(SolverSession& prop, unsigned depth,
                   const std::vector<std::tuple<Literal, Literal, Literal>>& gates,
                   const SatRarOptions& options, SatRarStatistics& stats) {
    if (depth == 0) {
        return prop.propagate(nullptr) == solver::QueryState::Open ? Rec::Ok : Rec::Conflict;
    }
    std::vector<Literal> learned;
    for (const auto& [out, x, y] : gates) {
        if (prop.value(out) != false || prop.value(x) || prop.value(y)) {
            continue;
        }
        const auto level = prop.contextLevel();
        std::vector<Literal> intersection;
        bool feasible = false;
        bool limited = false;
        for (const auto input : {x, y}) {
            if (!consumeTrial(options, stats, "split")) {
                limited = true;
                break;
            }
            prop.newContextLevel();
            const bool opened = prop.pushAssumption(~input);
            Rec nested = Rec::Conflict;
            if (opened) {
                nested = recursiveLearn(prop, depth - 1, gates, options, stats);
            }
            if (opened && nested == Rec::Ok) {
                auto branch = prop.trail();
                const auto byRaw = [](Literal a, Literal b) { return a.raw() < b.raw(); };
                std::sort(branch.begin(), branch.end(), byRaw);
                if (!feasible) {
                    intersection = std::move(branch);
                } else {
                    std::vector<Literal> common;
                    std::set_intersection(intersection.begin(), intersection.end(), branch.begin(),
                                          branch.end(), std::back_inserter(common), byRaw);
                    intersection = std::move(common);
                }
                feasible = true;
            } else if (nested == Rec::Limited) {
                limited = true;
            }
            prop.cancelToContextLevel(level);
            if (limited) {
                break;
            }
        }
        if (limited) {
            return Rec::Limited;
        }
        if (!feasible) {
            return Rec::Conflict;
        }
        learned.insert(learned.end(), intersection.begin(), intersection.end());
    }
    std::sort(learned.begin(), learned.end(), [](Literal a, Literal b) { return a.raw() < b.raw(); });
    learned.erase(std::unique(learned.begin(), learned.end()), learned.end());
    for (const auto lit : learned) {
        if (!prop.pushAssumption(lit)) {
            return Rec::Conflict;
        }
    }
    return Rec::Ok;
}

const char* conflictEvidence(std::size_t roots) {
    return roots == 1 ? "Theorem-1" : roots == 2 ? "Corollary-2.1" : "Generalized-2.1";
}

const char* reverseEvidence(std::size_t roots) { return roots == 1 ? "Theorem-2" : "Corollary-2.2"; }

std::vector<std::uint32_t> rawsOf(const std::vector<Literal>& lits) {
    std::vector<std::uint32_t> raws;
    for (const auto lit : lits) {
        raws.push_back(lit.raw());
    }
    std::sort(raws.begin(), raws.end());
    raws.erase(std::unique(raws.begin(), raws.end()), raws.end());
    return raws;
}

}  // namespace

std::string canonicalKey(const aig::Circuit& circuit, const Alternative& alternative) {
    std::ostringstream out;
    std::visit(
        [&](const auto& candidate) {
            const auto& pin = candidate.attachment.target;
            out << circuit.nodes().at(pin.wire.destination).aagVar << ',' << int(pin.wire.inputPin) << ','
                << (pin.stuckAt == analysis::StuckAt::One ? 1 : 0) << ','
                << circuit.nodes().at(candidate.attachment.dominator).aagVar << ',';
            if constexpr (std::is_same_v<std::decay_t<decltype(candidate)>, WireAlternative>) {
                out << "w," << candidate.source.raw();
            } else {
                auto conj = candidate.conjunction;
                auto disj = candidate.disjunction;
                std::sort(conj.begin(), conj.end(), [](Literal a, Literal b) { return a.raw() < b.raw(); });
                std::sort(disj.begin(), disj.end(), [](Literal a, Literal b) { return a.raw() < b.raw(); });
                out << "g";
                for (const auto lit : conj) {
                    out << ',' << lit.raw();
                }
                out << '/';
                for (std::size_t i = 0; i < disj.size(); ++i) {
                    out << (i ? "+" : "") << disj[i].raw();
                }
            }
        },
        alternative);
    return out.str();
}

std::vector<Alternative> SatRarEngine::run() {
    statistics_ = {};
    std::vector<Alternative> results;
    if (!target_.wire.valid()) {
        return results;
    }
    auto& prop = context_.session();
    const auto statsAtStart = prop.stats();
    const auto recordSessionVisits = [&] {
        statistics_.incidentGateVisits = prop.stats().incidentGateVisits - statsAtStart.incidentGateVisits;
        statistics_.learnedClauseVisits = prop.stats().learnedClauseVisits - statsAtStart.learnedClauseVisits;
    };
    context_.resetSolver();
    context_.resetProfile();
    const auto since = [](std::chrono::steady_clock::time_point began) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    };
    const auto& circuit = context_.circuit();
    const auto& graph = context_.graph();
    std::vector<std::tuple<Literal, Literal, Literal>> splits;
    for (const auto id : circuit.topologicalOrder()) {
        const auto& node = circuit.nodes()[id];
        if (node.isAnd()) {
            splits.emplace_back(Literal::fromVariable(node.aagVar), node.andInputs().left,
                                node.andInputs().right);
        }
    }
    std::reverse(splits.begin(), splits.end());
    const auto trace = [&](const std::string& line) {
        if (options_.trace) {
            options_.trace(line);
        }
    };
    const auto rawList = [](const std::vector<Literal>& lits) {
        std::ostringstream text;
        for (std::size_t i = 0; i < lits.size(); ++i) {
            text << (i ? "," : "") << lits[i].raw();
        }
        return text.str();
    };
    const unsigned depth = options_.mode == SatRarMode::TwoWayRarL2 ? 2
                           : options_.mode == SatRarMode::TwoWayRarL1 ? 1
                                                                      : 0;
    const bool satrar = options_.mode == SatRarMode::SrarWire || options_.mode == SatRarMode::SrarGate ||
                        options_.mode == SatRarMode::SrarSubCircuit;
    const bool gateMode = options_.mode == SatRarMode::SrarGate || options_.mode == SatRarMode::SrarSubCircuit;
    statistics_.limitScope = "target";
    context_.setPinFault(target_);
    auto targetMa = context_.computeMandatoryAssignments();
    const auto learnSnapshot = [&](analysis::MandatoryAssignments& ma) {
        if (context_.status() == analysis::FaultStatus::Conflict) {
            return false;
        }
        if (depth == 0) {
            return true;
        }
        const auto level = prop.contextLevel();
        prop.newContextLevel();
        const auto rec = recursiveLearn(prop, depth, splits, options_, statistics_);
        if (rec == Rec::Ok) {
            ma = context_.snapshot();
        }
        prop.cancelToContextLevel(level);
        return rec != Rec::Conflict;
    };
    if (!learnSnapshot(targetMa)) {
        statistics_.targetConflict = true;
        statistics_.redundancy.push_back({RedundancyEvent::Kind::Target, target_.wire.destination});
        recordSessionVisits();
        context_.clearFault();
        return results;
    }

    std::optional<SolverSession> targetOracle;
    if (options_.mode == SatRarMode::SrarSubCircuit) {
        targetOracle.emplace(context_.net());
        (void)targetOracle->setAssumptions(context_.requirements());
    }
    std::set<std::tuple<aig::NodeId, std::vector<std::uint32_t>, std::vector<std::uint32_t>>> seen;
    std::set<std::vector<std::uint32_t>> installed;
    const auto affected = graph.fanoutCone(target_.wire.destination);

    for (const auto dom : graph.dominatorsForWire(target_.wire)) {
        if (options_.excludeTargetDirectReceiverGates && dom == target_.wire.destination) {
            continue;
        }
        const auto domVar = circuit.nodes()[dom].aagVar;
        statistics_.limitScope = "dominator:" + std::to_string(domVar);
        context_.setDominatorOutputFault({dom, analysis::StuckAt::Zero});
        auto domMa = context_.computeMandatoryAssignments();
        if (!learnSnapshot(domMa)) {
            ++statistics_.redundantDominators;
            statistics_.redundancy.push_back({RedundancyEvent::Kind::Dominator, dom});
            continue;
        }
        const auto eligible = [&](Literal lit) {
            if (lit.isConstant()) {
                return false;
            }
            return affected.count(circuit.findNodeByAigVar(lit.variable())) == 0;
        };
        const auto emit = [&](std::vector<Literal> operands, std::vector<Literal> choices, const char* evidence) {
            const auto began = std::chrono::steady_clock::now();
            const auto lhs = rawsOf(operands);
            const auto rhs = rawsOf(choices);
            statistics_.profileCanonical += since(began);
            if (lhs.empty() || !seen.emplace(dom, lhs, rhs).second) {
                return false;
            }
            trace(std::string("emit\t") + evidence);
            Attachment attach{target_, dom, evidence};
            if (lhs.size() == 1 && rhs.empty()) {
                results.emplace_back(WireAlternative{attach, ~Literal(lhs.front())});
                return true;
            }
            GateAlternative gate{attach, {}, {}};
            for (const auto raw : lhs) {
                gate.conjunction.emplace_back(raw);
            }
            for (const auto raw : rhs) {
                gate.disjunction.emplace_back(raw);
            }
            results.emplace_back(std::move(gate));
            return true;
        };

        std::vector<Literal> decisions;
        for (const auto& entry : targetMa.items()) {
            const auto lit = entry.lit;
            if (!eligible(lit)) {
                continue;
            }
            if (domMa.valueOf(lit) == false) {
                emit({lit}, {}, "opposite-MA");
            } else if (!domMa.valueOf(lit)) {
                decisions.push_back(lit);
            }
        }
        if (!satrar) {
            continue;
        }

        std::vector<Literal> trial;
        std::unordered_set<std::uint32_t> filtered;
        bool baseDead = false;
        const auto trialSupport = [&](std::optional<Literal> implied) {
            const auto support = implied ? prop.decisionSupport(*implied).roots : prop.explainConflict().roots;
            std::vector<Literal> roots;
            for (const auto decision : trial) {
                if (std::find(support.begin(), support.end(), decision) != support.end()) {
                    roots.push_back(decision);
                }
            }
            return roots;
        };
        const auto install = [&](std::vector<Literal> clause) {
            auto key = rawsOf(clause);
            if (key.empty() || !installed.insert(key).second) {
                return !prop.inconsistent();
            }
            ++statistics_.learned;
            statistics_.learnedClauses.push_back(key);
            const auto before = prop.trail().size();
            const auto consistent = prop.learnNow(std::move(clause));
            if (prop.trail().size() > before) {
                statistics_.learnedUnits += prop.trail().size() - before;
                filtered.clear();
            }
            return consistent;
        };
        const auto trySubCircuit = [&] {
            if (!targetOracle) {
                return;
            }
            std::vector<Literal> choices;
            for (const auto lit : prop.trail()) {
                if (eligible(lit) && !domMa.valueOf(lit) && !targetMa.valueOf(lit)) {
                    choices.push_back(~lit);
                }
            }
            if (choices.size() < 2) {
                return;
            }
            const auto refuted = [&](const std::vector<Literal>& values, const char* step) -> std::optional<bool> {
                if (!consumeTrial(options_, statistics_, step)) {
                    return std::nullopt;
                }
                ++statistics_.subProbes;
                const auto mark = targetOracle->contextLevel();
                targetOracle->newContextLevel();
                bool consistent = true;
                for (const auto lit : values) {
                    if (!targetOracle->pushAssumption(~lit)) {
                        consistent = false;
                        break;
                    }
                }
                targetOracle->cancelToContextLevel(mark);
                return !consistent;
            };
            const auto whole = refuted(choices, "sub_probe");
            if (!whole || !*whole) {
                return;
            }
            for (std::size_t i = choices.size(); i-- > 0;) {
                auto fewer = choices;
                fewer.erase(fewer.begin() + static_cast<std::ptrdiff_t>(i));
                const auto retry = refuted(fewer, "sub_shrink");
                if (!retry) {
                    break;
                }
                if (*retry) {
                    choices = std::move(fewer);
                    ++statistics_.subReductions;
                }
            }
            if (choices.size() < 2) {
                return;
            }
            std::set<std::uint32_t> used;
            for (const auto choice : choices) {
                for (const auto root : trialSupport(~choice)) {
                    used.insert(root.raw());
                }
            }
            std::vector<Literal> conjunction;
            for (const auto decision : trial) {
                if (used.count(decision.raw()) != 0) {
                    conjunction.push_back(decision);
                }
            }
            if (!conjunction.empty() && emit(conjunction, choices, "Theorem-3")) {
                ++statistics_.subCandidates;
            }
        };

        const unsigned maxDepth = gateMode ? std::max(1u, options_.maxDecisionDepth) : 1;
        const auto explore = [&](auto&& self, std::size_t begin) -> void {
            for (std::size_t i = begin; i < decisions.size() && !statistics_.limitReached && !baseDead; ++i) {
                const auto lit = decisions[i];
                if (trial.empty() && options_.enableImplicationFilter && filtered.count(lit.raw()) != 0) {
                    ++statistics_.filtered;
                    continue;
                }
                const auto known = prop.value(lit);
                if (known == true) {
                    continue;
                }
                if (known == false) {
                    trial.push_back(lit);
                    auto roots = trialSupport(~lit);
                    roots.push_back(lit);
                    if (gateMode || roots.size() == 1) {
                        emit(roots, {}, conflictEvidence(roots.size()));
                    }
                    trial.pop_back();
                    continue;
                }
                if (!consumeTrial(options_, statistics_, "trial")) {
                    continue;
                }
                const auto mark = prop.contextLevel();
                const auto trailStart = prop.trail().size();
                prop.newContextLevel();
                trial.push_back(lit);
                statistics_.maxDepth = std::max<unsigned>(statistics_.maxDepth, static_cast<unsigned>(trial.size()));
                std::optional<std::vector<Literal>> clause;
                if (!prop.pushAssumption(lit)) {
                    auto roots = trialSupport(std::nullopt);
                    if (roots.empty()) {
                        baseDead = true;
                        ++statistics_.redundantDominators;
                        statistics_.redundancy.push_back({RedundancyEvent::Kind::Dominator, dom});
                    } else if (gateMode || roots.size() == 1) {
                        emit(roots, {}, conflictEvidence(roots.size()));
                    }
                    if (options_.enableLearning) {
                        auto premises = prop.explainConflict().roots;
                        for (auto& premise : premises) {
                            premise = ~premise;
                        }
                        clause = std::move(premises);
                    }
                } else {
                    if (gateMode) {
                        for (std::size_t j = trailStart; j < prop.trail().size(); ++j) {
                            const auto opposite = ~prop.trail()[j];
                            if (targetMa.valueOf(opposite) == true && eligible(opposite)) {
                                auto roots = trialSupport(~opposite);
                                if (!roots.empty()) {
                                    const auto kind = reverseEvidence(roots.size());
                                    roots.push_back(opposite);
                                    emit(std::move(roots), {}, kind);
                                }
                            }
                        }
                        trySubCircuit();
                    }
                    if (trial.size() == 1 && options_.enableImplicationFilter) {
                        for (std::size_t j = trailStart; j < prop.trail().size(); ++j) {
                            const auto other = prop.trail()[j];
                            if (other != lit && targetMa.valueOf(other) == true && !prop.isDecision(other)) {
                                filtered.insert(other.raw());
                            }
                        }
                    }
                    if (trial.size() < maxDepth && !statistics_.limitReached) {
                        self(self, i + 1);
                    }
                }
                trial.pop_back();
                prop.cancelToContextLevel(mark);
                if (clause && !baseDead && !install(std::move(*clause))) {
                    baseDead = true;
                    if (trial.empty()) {
                        ++statistics_.redundantDominators;
                        statistics_.redundancy.push_back({RedundancyEvent::Kind::Dominator, dom});
                    }
                }
            }
        };
        const auto trialsStart = std::chrono::steady_clock::now();
        explore(explore, 0);
        statistics_.profileTrials += since(trialsStart);
    }
    statistics_.profileProjection = context_.profile().projectionSeconds;
    statistics_.profileSetup = context_.profile().propagationSeconds;
    recordSessionVisits();
    context_.clearFault();
    return results;
}

}  // namespace satrar::rar
