#include "satrar/solver/SolverSession.hpp"

#include <algorithm>
#include <unordered_set>

namespace satrar::solver {
namespace {

constexpr std::size_t kNoTrail = static_cast<std::size_t>(-1);

std::vector<aig::Literal> normalized(std::vector<aig::Literal> clause, bool& tautology, bool& emptyConflict) {
    tautology = false;
    emptyConflict = false;
    std::vector<aig::Literal> out;
    std::unordered_set<std::uint32_t> seen;
    for (auto lit : clause) {
        if (lit.isConstant()) {
            if (lit.valueIfConstant()) {
                tautology = true;
                return {};
            }
            continue;
        }
        if (!seen.insert(lit.raw()).second) {
            continue;
        }
        if (seen.count((~lit).raw()) != 0) {
            tautology = true;
            return {};
        }
        out.push_back(lit);
    }
    if (out.empty()) {
        emptyConflict = true;
    }
    return out;
}

}  // namespace

bool Budget::chargeEvent() {
    if (events >= eventLimit) {
        return false;
    }
    ++events;
    return true;
}

bool Budget::chargeDecision() {
    if (decisions >= decisionLimit) {
        return false;
    }
    ++decisions;
    return true;
}

SolverSession::SolverSession(const GateNet& net) : net_(net) {
    values_.assign(net_.signalCount(), 0);
    trailIndex_.assign(net_.signalCount(), kNoTrail);
    clauseByVar_.assign(net_.signalCount(), {});
    constructing_ = true;
    seedRoot();
    constructing_ = false;
}

void SolverSession::seedRoot() {
    std::fill(values_.begin(), values_.end(), std::int8_t{0});
    values_[0] = -1;
    marks_.clear();
    marks_.push_back(Mark{0, 0, false});
    trail_.clear();
    trailLits_.clear();
    qhead_ = 0;
    inconsistent_ = false;
    clearConflict();
    std::fill(trailIndex_.begin(), trailIndex_.end(), kNoTrail);
    for (std::uint32_t g = 0; g < net_.gateCount(); ++g) {
        if (!propagateGate(g)) {
            break;
        }
    }
    (void)propagate(nullptr);
    stats_.rootAssignments = trail_.size();
    newContextLevel();
}

void SolverSession::resetAssignments() {
    const auto learned = clauses_;
    const auto touches = stats_.incidentGateVisits;
    const auto clauseVisits = stats_.learnedClauseVisits;
    const auto learnedCount = stats_.learnedClauses;
    stats_ = {};
    stats_.incidentGateVisits = touches;
    stats_.learnedClauseVisits = clauseVisits;
    stats_.learnedClauses = learnedCount;
    assumptions_.clear();
    clauses_.clear();
    for (auto& list : clauseByVar_) {
        list.clear();
    }
    seedRoot();
    for (const auto& clause : learned) {
        (void)addClause(clause);
        if (!inconsistent_) {
            (void)enforceClause(static_cast<std::uint32_t>(clauses_.size() - 1));
        }
    }
    if (!learned.empty()) {
        (void)propagate(nullptr);
    }
}

void SolverSession::forgetLearned() {
    clauses_.clear();
    for (auto& list : clauseByVar_) {
        list.clear();
    }
    stats_.learnedClauses = 0;
    assumptions_.clear();
    seedRoot();
}

std::uint32_t SolverSession::contextLevel() const noexcept {
    return marks_.empty() ? 0u : static_cast<std::uint32_t>(marks_.size() - 1);
}

void SolverSession::newContextLevel() {
    marks_.push_back(Mark{trail_.size(), qhead_, inconsistent_});
}

void SolverSession::cancelToContextLevel(std::uint32_t levelToKeep) {
    if (levelToKeep >= contextLevel()) {
        return;
    }
    const auto mark = marks_.at(levelToKeep + 1);
    std::vector<std::uint32_t> lost;
    while (trail_.size() > mark.trailSize) {
        const auto& entry = trail_.back();
        if (entry.kind == ReasonKind::Learned) {
            lost.push_back(entry.clauseId);
        }
        const auto dense = denseOf(entry.lit);
        if (dense != 0) {
            values_[dense] = 0;
            trailIndex_[dense] = kNoTrail;
        }
        trail_.pop_back();
        trailLits_.pop_back();
    }
    marks_.resize(levelToKeep + 1);
    qhead_ = std::min(mark.qhead, trail_.size());
    inconsistent_ = mark.inconsistent;
    clearConflict();
    if (assumptions_.size() > levelToKeep) {
        assumptions_.resize(levelToKeep);
    }
    for (const auto id : lost) {
        if (inconsistent_) {
            break;
        }
        (void)enforceClause(id);
    }
    if (!lost.empty() && !inconsistent_) {
        (void)propagate(nullptr);
    }
}

QueryState SolverSession::setAssumptions(const std::vector<aig::Literal>& ordered) {
    std::size_t common = 0;
    while (common < assumptions_.size() && common < ordered.size() && assumptions_[common] == ordered[common]) {
        ++common;
    }
    cancelToContextLevel(static_cast<std::uint32_t>(common));
    assumptions_.resize(common);
    for (std::size_t i = common; i < ordered.size(); ++i) {
        newContextLevel();
        assumptions_.push_back(ordered[i]);
        if (!pushAssumption(ordered[i])) {
            break;
        }
    }
    return inconsistent_ ? QueryState::Conflict : QueryState::Open;
}

Checkpoint SolverSession::mark() const { return Checkpoint{contextLevel()}; }

void SolverSession::rollback(Checkpoint point) { cancelToContextLevel(point.level); }

ProbeResult SolverSession::probe(aig::Literal decision, Budget& budget) {
    ProbeResult result;
    result.trailBegin = trail_.size();
    newContextLevel();
    if (!enqueue(decision, ReasonKind::Decision, {}, 0)) {
        result.state = QueryState::Conflict;
        return result;
    }
    result.state = propagate(&budget);
    return result;
}

bool SolverSession::pushAssumption(aig::Literal lit) {
    if (inconsistent_) {
        return false;
    }
    if (!enqueue(lit, ReasonKind::Decision, {}, 0)) {
        return false;
    }
    return propagate(nullptr) == QueryState::Open;
}

std::uint32_t SolverSession::denseOf(aig::Literal lit) const {
    if (lit.isConstant()) {
        return 0;
    }
    return net_.denseOfAig(lit.variable());
}

std::optional<bool> SolverSession::value(aig::Literal lit) const {
    if (lit.isConstant()) {
        return lit.valueIfConstant();
    }
    const auto state = values_.at(denseOf(lit));
    if (state == 0) {
        return std::nullopt;
    }
    const bool positive = state > 0;
    return lit.inverted() ? !positive : positive;
}

std::uint32_t SolverSession::level(aig::Literal lit) const {
    if (lit.isConstant()) {
        return 0;
    }
    const auto index = trailIndex_.at(denseOf(lit));
    return index == kNoTrail ? 0u : trail_.at(index).level;
}

bool SolverSession::isDecision(aig::Literal lit) const {
    if (lit.isConstant()) {
        return false;
    }
    const auto index = trailIndex_.at(denseOf(lit));
    return index != kNoTrail && trail_.at(index).kind == ReasonKind::Decision;
}

ReasonKind SolverSession::reasonKind(aig::Literal lit) const {
    if (lit.isConstant()) {
        return ReasonKind::Root;
    }
    const auto index = trailIndex_.at(denseOf(lit));
    if (index == kNoTrail) {
        return ReasonKind::Root;
    }
    return trail_.at(index).kind;
}

std::vector<aig::Literal> SolverSession::reasonClause(aig::Literal lit) const {
    if (lit.isConstant()) {
        return {};
    }
    const auto index = trailIndex_.at(denseOf(lit));
    if (index == kNoTrail) {
        return {};
    }
    return trail_.at(index).clause;
}

bool SolverSession::enqueue(aig::Literal lit, ReasonKind kind, std::vector<aig::Literal> clause,
                            std::uint32_t clauseId) {
    if (inconsistent_) {
        return false;
    }
    if (lit.isConstant()) {
        if (lit.valueIfConstant()) {
            return true;
        }
        inconsistent_ = true;
        conflict_.active = true;
        conflict_.fromDecision = kind == ReasonKind::Decision;
        conflict_.decision = lit;
        conflict_.clause = std::move(clause);
        return false;
    }
    const auto dense = denseOf(lit);
    const std::int8_t want = lit.inverted() ? -1 : 1;
    const auto have = values_[dense];
    if (have == want) {
        return true;
    }
    if (have == -want) {
        inconsistent_ = true;
        conflict_.active = true;
        conflict_.fromDecision = kind == ReasonKind::Decision;
        conflict_.decision = lit;
        if (kind == ReasonKind::Decision) {
            const auto index = trailIndex_[dense];
            conflict_.clause = index == kNoTrail ? std::vector<aig::Literal>{} : trail_[index].clause;
        } else {
            conflict_.clause = std::move(clause);
        }
        return false;
    }
    values_[dense] = want;
    trailIndex_[dense] = trail_.size();
    Entry entry;
    entry.lit = lit;
    entry.level = constructing_ ? 0u : contextLevel();
    entry.kind = kind;
    entry.clauseId = clauseId;
    entry.clause = std::move(clause);
    trail_.push_back(std::move(entry));
    trailLits_.push_back(lit);
    return true;
}

bool SolverSession::propagateGate(std::uint32_t index) {
    if (inconsistent_) {
        return false;
    }
    ++stats_.incidentGateVisits;
    const auto& gate = net_.gate(index);
    const auto logical = [&](GateNet::Edge edge) -> std::optional<bool> {
        if (values_[edge.var] == 0) {
            return std::nullopt;
        }
        const bool positive = values_[edge.var] > 0;
        return edge.negated ? !positive : positive;
    };
    const auto x = logical(gate.left);
    const auto y = logical(gate.right);
    const auto z = logical(GateNet::Edge{gate.out, false});
    const auto xLit = net_.literalOf(gate.left.var, gate.left.negated);
    const auto yLit = net_.literalOf(gate.right.var, gate.right.negated);
    const auto zLit = net_.literalOf(gate.out, false);
    const bool same = gate.left.var == gate.right.var && gate.left.negated == gate.right.negated;
    const bool opposite = gate.left.var == gate.right.var && gate.left.negated != gate.right.negated;

    auto imply = [&](aig::Literal lit, std::vector<aig::Literal> clause) {
        return enqueue(lit, constructing_ ? ReasonKind::Root : ReasonKind::Gate, std::move(clause), 0);
    };

    if (opposite || (x && y && *x == false && *y == false && xLit == ~yLit)) {
        if (z == true) {
            return imply(~zLit, {~zLit});
        }
        if (!z) {
            return imply(~zLit, {~zLit});
        }
        return true;
    }
    if (same) {
        if (x && z && *x != *z) {
            return imply(*x ? zLit : ~zLit, {*x ? ~xLit : xLit, *x ? zLit : ~zLit});
        }
        if (x && !z) {
            return imply(*x ? zLit : ~zLit, {*x ? ~xLit : xLit, *x ? zLit : ~zLit});
        }
        if (z && !x) {
            return imply(*z ? xLit : ~xLit, {*z ? ~zLit : zLit, *z ? xLit : ~xLit});
        }
        return true;
    }

    if (x == false) {
        if (z != false && !imply(~zLit, {~zLit, xLit})) {
            return false;
        }
    }
    if (y == false) {
        if (z != false && !imply(~zLit, {~zLit, yLit})) {
            return false;
        }
    }
    if (x == true && y == true) {
        if (z != true && !imply(zLit, {zLit, ~xLit, ~yLit})) {
            return false;
        }
    }
    if (z == true) {
        if (x != true && !imply(xLit, {~zLit, xLit})) {
            return false;
        }
        if (y != true && !imply(yLit, {~zLit, yLit})) {
            return false;
        }
    }
    if (z == false && x == true) {
        if (y != false && !imply(~yLit, {zLit, ~xLit, ~yLit})) {
            return false;
        }
    }
    if (z == false && y == true) {
        if (x != false && !imply(~xLit, {zLit, ~xLit, ~yLit})) {
            return false;
        }
    }
    return !inconsistent_;
}

bool SolverSession::enforceClause(std::uint32_t id) {
    if (inconsistent_ || id >= clauses_.size()) {
        return !inconsistent_;
    }
    ++stats_.learnedClauseVisits;
    const auto& clause = clauses_[id];
    aig::Literal unit = aig::Literal(0);
    int open = 0;
    for (const auto lit : clause) {
        const auto state = value(lit);
        if (state == true) {
            return true;
        }
        if (!state) {
            ++open;
            unit = lit;
        }
    }
    if (open == 0) {
        inconsistent_ = true;
        conflict_.active = true;
        conflict_.fromDecision = false;
        conflict_.clause = clause;
        return false;
    }
    if (open == 1) {
        return enqueue(unit, ReasonKind::Learned, clause, id);
    }
    return true;
}

bool SolverSession::addClause(std::vector<aig::Literal> clause) {
    bool tautology = false;
    bool emptyConflict = false;
    auto lits = normalized(std::move(clause), tautology, emptyConflict);
    if (tautology) {
        return false;
    }
    if (emptyConflict) {
        inconsistent_ = true;
        conflict_.active = true;
        conflict_.fromDecision = false;
        conflict_.clause.clear();
        clauses_.push_back({});
        ++stats_.learnedClauses;
        return true;
    }
    for (const auto& existing : clauses_) {
        if (existing == lits) {
            return false;
        }
    }
    const auto id = static_cast<std::uint32_t>(clauses_.size());
    for (const auto lit : lits) {
        clauseByVar_.at(denseOf(lit)).push_back(id);
    }
    clauses_.push_back(std::move(lits));
    ++stats_.learnedClauses;
    return true;
}

QueryState SolverSession::propagate(Budget* budget) {
    while (!inconsistent_ && qhead_ < trail_.size()) {
        if (budget && !budget->chargeEvent()) {
            return QueryState::Limit;
        }
        ++stats_.propagations;
        const auto var = denseOf(trail_[qhead_].lit);
        ++qhead_;
        for (const auto gate : net_.incident(var)) {
            if (!propagateGate(gate)) {
                return QueryState::Conflict;
            }
        }
        if (var < clauseByVar_.size()) {
            const auto watchers = clauseByVar_[var];
            for (const auto id : watchers) {
                if (!enforceClause(id)) {
                    return QueryState::Conflict;
                }
            }
        }
    }
    return inconsistent_ ? QueryState::Conflict : QueryState::Open;
}

void SolverSession::walk(aig::Literal assignedTrue, std::vector<aig::Literal>& support,
                         std::vector<char>& seen) const {
    if (assignedTrue.isConstant()) {
        return;
    }
    const auto dense = denseOf(assignedTrue);
    if (seen[dense]) {
        return;
    }
    seen[dense] = 1;
    const auto index = trailIndex_[dense];
    if (index == kNoTrail) {
        return;
    }
    const auto& entry = trail_[index];
    if (entry.kind == ReasonKind::Decision) {
        support.push_back(entry.lit);
        return;
    }
    for (const auto other : entry.clause) {
        if (other == entry.lit) {
            continue;
        }
        walk(~other, support, seen);
    }
}

Support SolverSession::decisionSupport(std::optional<aig::Literal> implied) const {
    Support support;
    std::vector<char> seen(net_.signalCount(), 0);
    if (implied) {
        if (value(*implied) == true) {
            walk(*implied, support.roots, seen);
        }
        return support;
    }
    if (!conflict_.active) {
        return support;
    }
    if (conflict_.fromDecision) {
        if (!conflict_.decision.isConstant()) {
            support.roots.push_back(conflict_.decision);
            walk(~conflict_.decision, support.roots, seen);
        }
        return support;
    }
    for (const auto lit : conflict_.clause) {
        walk(~lit, support.roots, seen);
    }
    return support;
}

Support SolverSession::explain(aig::Literal implied) const { return decisionSupport(implied); }

Support SolverSession::explainConflict() const { return decisionSupport(std::nullopt); }

bool SolverSession::learnNow(std::vector<aig::Literal> clause) {
    const auto before = clauses_.size();
    const bool added = addClause(std::move(clause));
    if (!added) {
        return !inconsistent_;
    }
    if (!inconsistent_) {
        (void)enforceClause(static_cast<std::uint32_t>(clauses_.size() - 1));
    }
    (void)before;
    return propagate(nullptr) == QueryState::Open;
}

std::vector<aig::Literal> SolverSession::analyze() const {
    std::vector<aig::Literal> clause;
    auto addLit = [&](aig::Literal lit) {
        if (lit.isConstant()) {
            return;
        }
        if (std::find(clause.begin(), clause.end(), lit) == clause.end()) {
            clause.push_back(lit);
        }
    };
    if (conflict_.fromDecision) {
        if (conflict_.decision.isConstant()) {
            return {};
        }
        const auto dense = denseOf(conflict_.decision);
        const auto index = trailIndex_[dense];
        if (index == kNoTrail || trail_[index].kind == ReasonKind::Decision) {
            return {};
        }
        const auto current = contextLevel();
        for (const auto lit : trail_[index].clause) {
            if (lit == trail_[index].lit) {
                addLit(conflict_.decision);
            } else {
                addLit(lit);
            }
        }
        (void)current;
    } else {
        for (const auto lit : conflict_.clause) {
            addLit(lit);
        }
    }

    auto levelOf = [&](aig::Literal lit) -> std::uint32_t {
        if (lit.isConstant() || value(lit) != false) {
            return 0;
        }
        return level(~lit);
    };
    const auto current = contextLevel();
    auto atCurrent = [&] {
        std::size_t count = 0;
        for (const auto lit : clause) {
            count += levelOf(lit) == current;
        }
        return count;
    };
    while (atCurrent() > 1) {
        std::size_t latest = 0;
        bool found = false;
        for (const auto lit : clause) {
            if (levelOf(lit) != current || isDecision(~lit)) {
                continue;
            }
            const auto index = trailIndex_[denseOf(lit)];
            if (!found || index > latest) {
                latest = index;
                found = true;
            }
        }
        if (!found) {
            break;
        }
        const auto pivotTrue = trail_[latest].lit;
        const auto pivotFalse = ~pivotTrue;
        std::vector<aig::Literal> next;
        for (const auto lit : clause) {
            if (lit != pivotFalse) {
                next.push_back(lit);
            }
        }
        for (const auto other : trail_[latest].clause) {
            if (other != pivotTrue && std::find(next.begin(), next.end(), other) == next.end()) {
                next.push_back(other);
            }
        }
        clause = std::move(next);
    }
    bool tautology = false;
    bool emptyConflict = false;
    return normalized(std::move(clause), tautology, emptyConflict);
}

std::uint32_t SolverSession::backjumpLevel(const std::vector<aig::Literal>& clause) const {
    const auto current = contextLevel();
    std::uint32_t highest = 0;
    std::uint32_t second = 0;
    for (const auto lit : clause) {
        if (lit.isConstant() || value(lit) != false) {
            continue;
        }
        const auto lv = level(~lit);
        if (lv >= highest) {
            second = highest;
            highest = lv;
        } else if (lv > second) {
            second = lv;
        }
    }
    (void)current;
    return second;
}

SolveResult SolverSession::solve(const std::vector<aig::Literal>& assumptions, Budget& budget) {
    SolveResult result;
    const auto visitsAtStart = stats_.incidentGateVisits;
    auto pack = [&](SolveState state, const char* reason) {
        result.state = state;
        result.reason = reason;
        result.decisions = budget.decisions;
        result.bcpVisits = stats_.incidentGateVisits - visitsAtStart;
        result.learned = clauses_;
        if (state == SolveState::Sat) {
            result.primaryInputs.assign(net_.primaryInputs().size(), false);
            for (std::size_t i = 0; i < net_.primaryInputs().size(); ++i) {
                result.primaryInputs[i] = values_[net_.primaryInputs()[i]] > 0;
            }
        }
        return result;
    };
    if (setAssumptions(assumptions) == QueryState::Conflict) {
        return pack(SolveState::Unsat, "assumption_conflict");
    }
    const auto assumptionLevel = contextLevel();
    struct Frame {
        std::uint32_t parent = 0;
        aig::Literal lit;
        bool flipped = false;
    };
    std::vector<Frame> frames;

    auto pickUnassigned = [&]() -> std::optional<std::uint32_t> {
        for (const auto dense : net_.primaryInputs()) {
            if (values_[dense] == 0) {
                return dense;
            }
        }
        for (std::uint32_t g = 0; g < net_.gateCount(); ++g) {
            const auto dense = net_.gate(g).out;
            if (values_[dense] == 0) {
                return dense;
            }
        }
        return std::nullopt;
    };

    while (true) {
        if (inconsistent_) {
            ++result.conflicts;
            if (cdcl_) {
                if (contextLevel() <= assumptionLevel) {
                    return pack(SolveState::Unsat, "conflict");
                }
                const auto learned = analyze();
                auto bj = learned.empty() ? assumptionLevel : backjumpLevel(learned);
                if (bj < assumptionLevel) {
                    bj = assumptionLevel;
                }
                while (!frames.empty() && frames.back().parent >= bj) {
                    frames.pop_back();
                }
                cancelToContextLevel(bj);
                if (!learned.empty()) {
                    (void)learnNow(learned);
                }
                if (inconsistent_ && contextLevel() <= assumptionLevel) {
                    return pack(SolveState::Unsat, "conflict");
                }
                continue;
            }
            bool recovered = false;
            while (!frames.empty()) {
                const auto frame = frames.back();
                frames.pop_back();
                cancelToContextLevel(frame.parent);
                if (frame.flipped) {
                    continue;
                }
                if (!budget.chargeDecision()) {
                    return pack(SolveState::Unknown, "decision_limit");
                }
                newContextLevel();
                const auto flipped = ~frame.lit;
                const bool queued = enqueue(flipped, ReasonKind::Decision, {}, 0);
                const auto propagated = queued ? propagate(&budget) : QueryState::Conflict;
                if (propagated == QueryState::Limit) {
                    return pack(SolveState::Unknown, "event_limit");
                }
                if (queued && propagated == QueryState::Open) {
                    frames.push_back(Frame{frame.parent, flipped, true});
                    recovered = true;
                    break;
                }
                ++result.conflicts;
                cancelToContextLevel(frame.parent);
            }
            if (!recovered) {
                return pack(SolveState::Unsat, "conflict");
            }
            continue;
        }

        const auto pick = pickUnassigned();
        if (!pick) {
            return pack(SolveState::Sat, "model");
        }
        if (!budget.chargeDecision()) {
            return pack(SolveState::Unknown, "decision_limit");
        }
        const auto parent = contextLevel();
        const auto lit = ~net_.literalOf(*pick, false);
        newContextLevel();
        frames.push_back(Frame{parent, lit, false});
        const bool queued = enqueue(lit, ReasonKind::Decision, {}, 0);
        const auto propagated = queued ? propagate(&budget) : QueryState::Conflict;
        if (propagated == QueryState::Limit) {
            return pack(SolveState::Unknown, "event_limit");
        }
    }
}

}  // namespace satrar::solver
