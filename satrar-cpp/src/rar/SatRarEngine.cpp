
#include "satrar/rar/SatRarEngine.hpp"

#include "satrar/analysis/GraphAnalysis.hpp"
#include "satrar/sat/CnfEncoder.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_set>

namespace satrar::rar {
namespace {

using sat::DimacsLiteral;
using analysis::CircuitPropagator;

bool consumeTrial(const SatRarOptions& options, SatRarStatistics& stats, const char* step) {
    if (stats.trials >= options.maxTrials) {
        if (!stats.limitReached) {
            stats.limitReached = true;
            stats.limitStep = step;
            stats.limitScope = stats.limitScope.empty() ? "target" : stats.limitScope;
        }
        return false;
    }
    ++stats.trials;
    return true;
}

bool recursiveLearn(CircuitPropagator& prop, unsigned depth,
                    const std::vector<std::pair<DimacsLiteral, std::pair<DimacsLiteral, DimacsLiteral>>>& gates,
                    const SatRarOptions& options, SatRarStatistics& stats) {
    if (!depth) {
        return prop.propagate();
    }
    std::set<DimacsLiteral> learned;
    for (const auto& [out, inputs] : gates) {
        const auto [x, y] = inputs;
        if (prop.value(out) != false || prop.value(x) || prop.value(y)) {
            continue;
        }
        const auto level = prop.contextLevel();
        std::vector<DimacsLiteral> intersection;
        bool feasible = false;
        for (const auto input : {x, y}) {
            // A budget stop never counts as having proved both branches: nothing is learned.
            if (!consumeTrial(options, stats, "split")) {
                return true;
            }
            prop.newContextLevel();
            if (prop.pushAssumption(-input) && recursiveLearn(prop, depth - 1, gates, options, stats)) {
                auto branch = prop.trail();
                std::sort(branch.begin(), branch.end());
                if (!feasible) {
                    intersection = std::move(branch);
                } else {
                    std::vector<DimacsLiteral> common;
                    std::set_intersection(intersection.begin(), intersection.end(),
                                          branch.begin(), branch.end(), std::back_inserter(common));
                    intersection = std::move(common);
                }
                feasible = true;
            }
            prop.cancelToContextLevel(level);
            if (stats.limitReached) {
                return true;
            }
        }
        if (!feasible) {
            return false;
        }
        learned.insert(intersection.begin(), intersection.end());
    }
    for (const auto lit : learned) {
        if (!prop.pushAssumption(lit)) {
            return false;
        }
    }
    return true;
}

const char* conflictEvidence(std::size_t roots) {
    // One supporting decision is Theorem 1, two is Corollary 2.1; more is the same implication
    // argument generalized to a longer conjunction.
    return roots == 1 ? "Theorem-1" : roots == 2 ? "Corollary-2.1" : "Generalized-2.1";
}

const char* reverseEvidence(std::size_t roots) {
    return roots == 1 ? "Theorem-2" : "Corollary-2.2";
}

}  // namespace

std::vector<Alternative> SatRarEngine::run() {
    statistics_ = {};
    std::vector<Alternative> results;
    if (!target_.wire.valid()) {
        return results;
    }
    // Every run starts cold: learned clauses never leak across faults, modes or ablations.
    context_.resetSolver();
    context_.resetProfile();
    const auto since = [](std::chrono::steady_clock::time_point began) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    };

    const auto& circuit = context_.circuit();
    const auto& cnf = context_.cnf();
    auto& prop = context_.propagator();
    const auto& graph = context_.graph();
    const auto literal = [&](aig::Literal edge) {
        return sat::CnfEncoder::dimacsLitForAig(cnf, edge).value();
    };
    if (signals_.empty()) {
        signals_.resize(cnf.numVariables() + 1);
        for (const auto id : circuit.topologicalOrder()) {
            const auto& node = circuit.nodes()[id];
            if (node.isPrimaryInput() || node.isAnd()) {
                const auto edge = aig::Literal::fromVariable(node.aagVar);
                const auto pos = literal(edge);
                signals_[sat::dimacsVariable(pos)] = edge;
                if (node.isAnd()) {
                    splits_.push_back({pos, {literal(node.andInputs().left), literal(node.andInputs().right)}});
                }
            }
        }
        std::reverse(splits_.begin(), splits_.end());
    }
    const auto edgeFor = [&](DimacsLiteral lit) {
        const auto edge = signals_[sat::dimacsVariable(lit)];
        return lit < 0 ? ~edge : edge;
    };
    const auto trace = [&](const std::string& line) {
        if (options_.trace) {
            options_.trace(line);
        }
    };
    const auto rawList = [&](const std::vector<DimacsLiteral>& lits) {
        std::ostringstream text;
        for (std::size_t i = 0; i < lits.size(); ++i) {
            text << (i ? "," : "") << edgeFor(lits[i]).raw();
        }
        return text.str();
    };

    const unsigned depth = options_.mode == SatRarMode::TwoWayRarL2 ? 2 :
                           options_.mode == SatRarMode::TwoWayRarL1 ? 1 : 0;
    const bool satrar = options_.mode == SatRarMode::SrarWire ||
                        options_.mode == SatRarMode::SrarGate ||
                        options_.mode == SatRarMode::SrarSubCircuit;
    const bool gateMode = options_.mode == SatRarMode::SrarGate || options_.mode == SatRarMode::SrarSubCircuit;
    statistics_.limitScope = "target";
    trace("scope\ttarget\tvar=" + std::to_string(circuit.nodes()[target_.wire.destination].aagVar) +
          "\tpin=" + std::to_string(target_.wire.inputPin) +
          "\tsa" + std::to_string(static_cast<int>(target_.stuckAt)));
    context_.setPinFault(target_);
    auto targetMa = context_.computeMandatoryAssignments();
    const auto learnSnapshot = [&](analysis::MandatoryAssignments& ma) {
        if (context_.status() == analysis::FaultStatus::Conflict) {
            return false;
        }
        if (!depth) {
            return true;
        }
        const auto level = prop.contextLevel();
        prop.newContextLevel();
        const auto consistent = recursiveLearn(prop, depth, splits_, options_, statistics_);
        if (consistent) {
            ma = context_.snapshot();
        }
        prop.cancelToContextLevel(level);
        return consistent;
    };
    if (!learnSnapshot(targetMa)) {
        statistics_.targetConflict = true;
        statistics_.redundancy.push_back({RedundancyEvent::Kind::Target, target_.wire.destination});
        trace("redundant_target\tvar=" + std::to_string(circuit.nodes()[target_.wire.destination].aagVar));
        context_.clearFault();
        return results;
    }

    std::unique_ptr<CircuitPropagator> targetOracle;
    if (options_.mode == SatRarMode::SrarSubCircuit) {
        targetOracle = std::make_unique<CircuitPropagator>(circuit);
        targetOracle->setAssumptions(context_.requirements());
    }
    std::set<std::tuple<aig::NodeId, std::vector<std::uint32_t>, std::vector<std::uint32_t>>> seen;
    std::set<std::vector<DimacsLiteral>> installed;  // Distinct circuit-valid clauses this run.
    const auto affected = graph.fanoutCone(target_.wire.destination);
    for (const auto dom : graph.dominatorsForWire(target_.wire)) {
        if (options_.excludeTargetDirectReceiverGates && dom == target_.wire.destination) {
            continue;
        }
        const auto domVar = circuit.nodes()[dom].aagVar;
        statistics_.limitScope = "dominator:" + std::to_string(domVar);
        trace("scope\tdominator\tvar=" + std::to_string(domVar));
        context_.setDominatorOutputFault({dom, analysis::StuckAt::Zero});
        auto domMa = context_.computeMandatoryAssignments();
        if (!learnSnapshot(domMa)) {
            ++statistics_.redundantDominators;
            statistics_.redundancy.push_back({RedundancyEvent::Kind::Dominator, dom});
            trace("redundant_dominator\tvar=" + std::to_string(domVar));
            continue;
        }
        const auto eligible = [&](DimacsLiteral lit) {
            const auto edge = edgeFor(lit);
            if (signals_[sat::dimacsVariable(lit)].isConstant()) {
                return false;
            }
            const auto source = circuit.findNodeByAigVar(edge.variable());
            // The inserted condition must have the same value in good and faulty circuits.
            return !affected.count(source);
        };

        const auto emit = [&](std::vector<DimacsLiteral> operands, std::vector<DimacsLiteral> choices,
                              const char* evidence) {
            const auto canonicalStart = std::chrono::steady_clock::now();
            struct Timer {
                double& total;
                std::chrono::steady_clock::time_point began;
                ~Timer() { total += std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count(); }
            } canonicalTimer{statistics_.profileCanonical, canonicalStart};
            const auto convert = [&](const std::vector<DimacsLiteral>& lits) {
                std::vector<std::uint32_t> sorted;
                for (const auto lit : lits) {
                    sorted.push_back(edgeFor(lit).raw());
                }
                std::sort(sorted.begin(), sorted.end());
                sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
                return sorted;
            };
            const auto lhs = convert(operands);
            const auto rhs = convert(choices);
            if (lhs.empty() || !seen.emplace(dom, lhs, rhs).second) {
                return false;
            }
            trace(std::string("emit\t") + evidence + "\tdominator=" + std::to_string(domVar) +
                  "\toperands=" + rawList(operands) + "\tchoices=" + rawList(choices));
            Attachment attach{target_, dom, evidence};
            if (lhs.size() == 1 && rhs.empty()) {
                results.emplace_back(WireAlternative{attach, ~aig::Literal(lhs.front())});
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

        std::vector<DimacsLiteral> decisions;
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

        std::vector<DimacsLiteral> trial;
        std::unordered_set<DimacsLiteral> filtered;
        bool baseDead = false;
        const auto trialSupport = [&](DimacsLiteral implied) {
            const auto support = prop.decisionSupport(implied);
            std::vector<DimacsLiteral> roots;
            for (const auto d : trial) {
                if (std::find(support.begin(), support.end(), d) != support.end()) {
                    roots.push_back(d);
                }
            }
            return roots;
        };

        // Install a circuit-valid clause at the current (already backtracked) level so that the
        // very next probe can use it (Figure 5 line 12-24: learning takes effect immediately).
        const auto install = [&](std::vector<DimacsLiteral> clause) {
            std::sort(clause.begin(), clause.end());
            clause.erase(std::unique(clause.begin(), clause.end()), clause.end());
            if (clause.empty() || !installed.insert(clause).second) {
                return true;
            }
            ++statistics_.learned;
            statistics_.learnedClauses.push_back(clause);
            const auto before = prop.trail().size();
            const auto consistent = prop.learnNow(clause);
            if (prop.trail().size() > before) {
                statistics_.learnedUnits += prop.trail().size() - before;
                // The base changed: earlier implication-filter conclusions may no longer hold.
                filtered.clear();
            }
            trace("learn\tpremises=" + std::to_string(clause.size()) +
                  "\tnew_assignments=" + std::to_string(prop.trail().size() - before));
            return consistent;
        };

        const auto trySubCircuit = [&] {
            if (!targetOracle) {
                return;
            }
            // b_i = NOT(l_i) for every eligible literal l_i the dominator side derives from the trial.
            std::vector<DimacsLiteral> choices;
            for (const auto lit : prop.trail()) {
                if (eligible(lit) && !domMa.valueOf(lit) && !targetMa.valueOf(lit)) {
                    choices.push_back(-lit);
                }
            }
            if (choices.size() < 2) {
                return;
            }
            // Target side: Phi_t AND all l_i must conflict, i.e. some b_i holds under the target.
            // Returns nullopt when the shared budget is exhausted before the probe could run.
            const auto refuted = [&](const std::vector<DimacsLiteral>& values,
                                     const char* step) -> std::optional<bool> {
                if (!consumeTrial(options_, statistics_, step)) {
                    return std::nullopt;
                }
                ++statistics_.subProbes;
                const auto mark = targetOracle->contextLevel();
                targetOracle->newContextLevel();
                bool consistent = true;
                for (const auto lit : values) {
                    if (!targetOracle->pushAssumption(-lit)) {
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
            // Drop operands that are not needed; every kept proof was fully completed.
            for (std::size_t i = choices.size(); i-- > 0;) {
                auto fewer = choices;
                fewer.erase(fewer.begin() + static_cast<std::ptrdiff_t>(i));
                const auto retry = refuted(fewer, "sub_shrink");
                if (!retry) {
                    break;  // Budget exhausted: the current B is still a completed proof.
                }
                if (*retry) {
                    choices = std::move(fewer);
                    ++statistics_.subReductions;
                }
            }
            if (choices.size() < 2) {
                return;
            }
            // A keeps only the trial decisions that the dominator side used to derive some b_i.
            std::set<DimacsLiteral> used;
            for (const auto b : choices) {
                for (const auto root : trialSupport(-b)) {
                    used.insert(root);
                }
            }
            std::vector<DimacsLiteral> conjunction;
            for (const auto d : trial) {
                if (used.count(d)) {
                    conjunction.push_back(d);
                }
            }
            if (!conjunction.empty() && emit(conjunction, choices, "Theorem-3")) {
                ++statistics_.subCandidates;
            }
        };

        const unsigned maxDepth = gateMode ? std::max(1u, options_.maxDecisionDepth) : 1;
        std::function<void(std::size_t)> explore = [&](std::size_t begin) {
            for (std::size_t i = begin; i < decisions.size() && !statistics_.limitReached && !baseDead; ++i) {
                const auto lit = decisions[i];
                if (trial.empty() && options_.enableImplicationFilter && filtered.count(lit)) {
                    ++statistics_.filtered;
                    continue;
                }
                // Already implied true: nothing to learn. A literal that learning made false is
                // still tried, because its conflict proves a wire.
                const auto known = prop.value(lit);
                if (known == true) {
                    continue;
                }
                if (known == false) {
                    // Already refuted by the context (a learned clause or an earlier trial): this is a
                    // conflict proof that needs no search, so it neither costs budget nor recurses.
                    trial.push_back(lit);
                    auto roots = trialSupport(-lit);
                    roots.push_back(lit);
                    trace("refuted\tdecision=" + rawList({lit}) + "\troots=" + rawList(roots));
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
                trace("trial\tdepth=" + std::to_string(trial.size()) + "\tdecision=" + rawList({lit}));
                std::optional<std::vector<DimacsLiteral>> clause;
                if (!prop.pushAssumption(lit)) {
                    auto roots = trialSupport(0);
                    trace("conflict\troots=" + rawList(roots));
                    if (roots.empty()) {
                        // The context itself became contradictory (learned rules): the dominator
                        // is redundant for this base. No alternative is derived from it.
                        baseDead = true;
                        ++statistics_.redundantDominators;
                        statistics_.redundancy.push_back({RedundancyEvent::Kind::Dominator, dom});
                        trace("redundant_dominator\tvar=" + std::to_string(domVar));
                    } else if (gateMode || roots.size() == 1) {
                        emit(roots, {}, conflictEvidence(roots.size()));
                    }
                    if (options_.enableLearning) {
                        auto premises = prop.decisionSupport();
                        for (auto& premise : premises) {
                            premise = -premise;
                        }
                        clause = std::move(premises);
                    }
                } else {
                    if (gateMode) {
                        for (auto j = trailStart; j < prop.trail().size(); ++j) {
                            const auto opposite = -prop.trail()[j];
                            if (targetMa.valueOf(opposite) == true && eligible(opposite)) {
                                auto roots = trialSupport(-opposite);
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
                        for (auto j = trailStart; j < prop.trail().size(); ++j) {
                            const auto other = prop.trail()[j];
                            // Only values derived by a gate reason from this probe qualify.
                            if (other != lit && targetMa.valueOf(other) == true &&
                                !prop.reason(other).empty()) {
                                filtered.insert(other);
                            }
                        }
                    }
                    if (trial.size() < maxDepth && !statistics_.limitReached) {
                        explore(i + 1);
                    }
                }
                trial.pop_back();
                prop.cancelToContextLevel(mark);
                if (clause && !baseDead && !install(std::move(*clause))) {
                    baseDead = true;
                    if (trial.empty()) {
                        ++statistics_.redundantDominators;
                        statistics_.redundancy.push_back({RedundancyEvent::Kind::Dominator, dom});
                        trace("redundant_dominator\tvar=" + std::to_string(domVar));
                    }
                }
            }
        };
        const auto trialsStart = std::chrono::steady_clock::now();
        explore(0);
        statistics_.profileTrials += since(trialsStart);
    }
    statistics_.profileProjection = context_.profile().projectionSeconds;
    statistics_.profileSetup = context_.profile().propagationSeconds;
    context_.clearFault();
    return results;
}

}  // namespace satrar::rar
