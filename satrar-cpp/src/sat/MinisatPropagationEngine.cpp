#include "satrar/sat/MinisatPropagationEngine.hpp"

#include "minisat/core/Solver.h"
#include "minisat/mtl/Vec.h"

#include <memory>
#include <stdexcept>

namespace satrar::sat {

namespace {

DimacsLiteral flip(DimacsLiteral lit) { return -lit; }

Minisat::Lit toMsLit(DimacsLiteral lit) {
    const int v = lit > 0 ? lit : -lit;
    return Minisat::mkLit(static_cast<Minisat::Var>(v - 1), lit < 0);
}

DimacsLiteral fromMsLit(const Minisat::Lit& lit) {
    const int v = Minisat::var(lit) + 1;
    return Minisat::sign(lit) ? -v : v;
}

}  // namespace

struct MinisatPropagationEngine::Impl {
    Minisat::Solver solver;
    std::uint32_t contextLevel = 0;
    std::vector<std::uint32_t> contextMarks{0};
    std::vector<DimacsLiteral> activeAssumptions;

    bool impliesConsistent(DimacsLiteral lit) {
        Minisat::vec<Minisat::Lit> trials;
        trials.growTo(static_cast<int>(activeAssumptions.size() + 1));
        for (std::size_t i = 0; i < activeAssumptions.size(); ++i) {
            trials[static_cast<int>(i)] = toMsLit(activeAssumptions[i]);
        }
        trials[static_cast<int>(activeAssumptions.size())] = toMsLit(lit);
        Minisat::vec<Minisat::Lit> out;
        return solver.implies(trials, out);
    }
};

MinisatPropagationEngine::MinisatPropagationEngine() : impl_(std::make_unique<Impl>()) {}

MinisatPropagationEngine::~MinisatPropagationEngine() = default;

MinisatPropagationEngine::MinisatPropagationEngine(MinisatPropagationEngine&&) noexcept = default;

MinisatPropagationEngine& MinisatPropagationEngine::operator=(MinisatPropagationEngine&&) noexcept =
    default;

std::uint32_t MinisatPropagationEngine::contextLevel() const noexcept {
    return impl_->contextLevel;
}

void MinisatPropagationEngine::loadBaseCnf(const CnfFormula& formula) {
    const std::uint32_t n = formula.numVariables();
    while (static_cast<std::uint32_t>(impl_->solver.nVars()) < n) {
        impl_->solver.newVar();
    }
    for (const auto& clause : formula.clauses()) {
        Minisat::vec<Minisat::Lit> ps;
        ps.growTo(static_cast<int>(clause.size()));
        for (int i = 0; i < ps.size(); ++i) {
            ps[i] = toMsLit(clause[static_cast<std::size_t>(i)]);
        }
        if (!impl_->solver.addClause(ps)) {
            throw std::runtime_error("failed to add clause to MiniSat");
        }
    }
    impl_->activeAssumptions.clear();
    impl_->contextLevel = 0;
    impl_->contextMarks.assign(1, 0);
}

void MinisatPropagationEngine::newContextLevel() {
    ++impl_->contextLevel;
    impl_->contextMarks.push_back(static_cast<std::uint32_t>(impl_->activeAssumptions.size()));
}

void MinisatPropagationEngine::cancelToContextLevel(std::uint32_t level) {
    if (level >= impl_->contextMarks.size()) {
        level = static_cast<std::uint32_t>(impl_->contextMarks.size() - 1);
    }
    impl_->activeAssumptions.resize(impl_->contextMarks[level]);
    impl_->contextMarks.resize(level + 1);
    impl_->contextLevel = level;
}

bool MinisatPropagationEngine::pushAssumption(DimacsLiteral lit) {
    if (!impl_->impliesConsistent(lit)) {
        return false;
    }
    impl_->activeAssumptions.push_back(lit);
    return true;
}

bool MinisatPropagationEngine::propagate(std::vector<DimacsLiteral>* outPropagated) {
    Minisat::vec<Minisat::Lit> assumps;
    assumps.growTo(static_cast<int>(impl_->activeAssumptions.size()));
    for (std::size_t i = 0; i < impl_->activeAssumptions.size(); ++i) {
        assumps[static_cast<int>(i)] = toMsLit(impl_->activeAssumptions[i]);
    }
    Minisat::vec<Minisat::Lit> out;
    if (!impl_->solver.implies(assumps, out)) {
        return false;
    }
    if (outPropagated) {
        outPropagated->clear();
        outPropagated->reserve(static_cast<std::size_t>(out.size()));
        for (int i = 0; i < out.size(); ++i) {
            outPropagated->push_back(fromMsLit(out[i]));
        }
    }
    return true;
}

std::optional<bool> MinisatPropagationEngine::value(DimacsLiteral lit) {
    const bool pos = impl_->impliesConsistent(lit);
    const bool neg = impl_->impliesConsistent(flip(lit));
    if (pos && !neg) {
        return true;
    }
    if (!pos && neg) {
        return false;
    }
    return std::nullopt;
}

std::optional<DimacsLiteral> MinisatPropagationEngine::reasonOf(DimacsLiteral) {
    return std::nullopt;
}

bool MinisatPropagationEngine::runSmokeSelfTest() {
    CnfFormula f;
    const std::uint32_t a = f.ensureAigVar(1);
    const std::uint32_t b = f.ensureAigVar(2);
    f.addClause({dimacsFromVar(a, true), dimacsFromVar(b, true)});

    MinisatPropagationEngine engine;
    engine.loadBaseCnf(f);
    if (!engine.pushAssumption(dimacsFromVar(a, false))) {
        return false;
    }
    std::vector<DimacsLiteral> implied;
    if (!engine.propagate(&implied) || implied.empty()) {
        return false;
    }
    const auto valB = engine.value(dimacsFromVar(b, true));
    return valB.has_value() && *valB;
}

}  // namespace satrar::sat