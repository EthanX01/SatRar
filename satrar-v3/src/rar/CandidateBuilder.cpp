#include "satrar/rar/CandidateBuilder.hpp"

#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace satrar::rar {
namespace {

std::uint32_t resolveLit(std::uint32_t lit, const std::vector<std::uint32_t>& repr) {
    const auto mapped = repr[lit >> 1u];
    return (lit & 1u) ? (mapped ^ 1u) : mapped;
}

}  // namespace

CandidateBuilder::CandidateBuilder(std::shared_ptr<const GoldenContext> golden)
    : golden_(std::move(golden)) {
    if (!golden_) throw std::invalid_argument("candidate builder needs a golden circuit");
}

CandidateNetlist CandidateBuilder::build(const Alternative& alternative) {
    const auto& golden = *golden_;
    const auto nodes = golden.numNodes();
    const analysis::PinFault target = std::visit(
        [](const auto& item) { return item.attachment.target; }, alternative);
    const auto dominator = std::visit(
        [](const auto& item) { return item.attachment.dominator; }, alternative);
    if (target.wire.destination >= golden.denseOfNode.size() ||
        dominator >= golden.denseOfNode.size()) {
        throw std::invalid_argument("candidate node is outside the golden circuit");
    }
    const auto dest = golden.denseOfNode[target.wire.destination];
    const auto dom = golden.denseOfNode[dominator];
    if (dest == aig::kInvalidNodeId || dom == aig::kInvalidNodeId || dest <= golden.numInputs ||
        dom <= golden.numInputs || target.wire.inputPin > 1) {
        throw std::invalid_argument("candidate attachment is not an AND pin");
    }
    const auto stuck = static_cast<std::uint32_t>(target.stuckAt);

    std::vector<std::pair<std::uint32_t, std::uint32_t>> extras;
    const auto addExtra = [&](std::uint32_t left, std::uint32_t right) {
        const auto variable = nodes + static_cast<std::uint32_t>(extras.size());
        extras.emplace_back(left, right);
        return variable * 2u;
    };
    std::uint32_t condition = 0;
    std::visit(
        [&](const auto& item) {
            using Kind = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Kind, WireAlternative>) {
                condition = golden.denseLit(item.source);
            } else {
                if (item.conjunction.empty()) {
                    throw std::invalid_argument("gate candidate has no operands");
                }
                auto term = golden.denseLit(item.conjunction.front());
                for (std::size_t index = 1; index < item.conjunction.size(); ++index) {
                    term = addExtra(term, golden.denseLit(item.conjunction[index]));
                }
                if (!item.disjunction.empty()) {
                    auto complement = golden.denseLit(item.disjunction.front()) ^ 1u;
                    for (std::size_t index = 1; index < item.disjunction.size(); ++index) {
                        complement = addExtra(complement, golden.denseLit(item.disjunction[index]) ^ 1u);
                    }
                    term = addExtra(term, complement ^ 1u);
                }
                condition = term ^ 1u;
            }
        },
        alternative);
    const auto wrapper = nodes + static_cast<std::uint32_t>(extras.size());
    extras.emplace_back(dom * 2u, condition);
    const auto vars = nodes + static_cast<std::uint32_t>(extras.size());

    const auto rewrite = [&](std::uint32_t lit) {
        if ((lit >> 1u) != dom) return lit;
        return (wrapper << 1u) | (lit & 1u);
    };

    in0_.assign(vars, 0);
    in1_.assign(vars, 0);
    for (std::uint32_t variable = golden.numInputs + 1u; variable < nodes; ++variable) {
        in0_[variable] = rewrite(golden.fanin0[variable]);
        in1_[variable] = rewrite(golden.fanin1[variable]);
    }
    if (target.wire.inputPin == 0) in0_[dest] = stuck;
    else in1_[dest] = stuck;
    for (std::uint32_t index = 0; index < extras.size(); ++index) {
        in0_[nodes + index] = extras[index].first;
        in1_[nodes + index] = extras[index].second;
    }
    std::vector<std::uint32_t> structuralOutputs(golden.outputs.size());
    for (std::size_t index = 0; index < golden.outputs.size(); ++index) {
        structuralOutputs[index] = rewrite(golden.outputs[index]);
    }

    indeg_.assign(vars, 0);
    succStart_.assign(static_cast<std::size_t>(vars) + 1u, 0);
    const auto note = [&](std::uint32_t lit, std::uint32_t user) {
        const auto driver = lit >> 1u;
        if (driver <= golden.numInputs) return;
        ++indeg_[user];
        ++succStart_[driver + 1u];
    };
    for (std::uint32_t variable = golden.numInputs + 1u; variable < vars; ++variable) {
        note(in0_[variable], variable);
        note(in1_[variable], variable);
    }
    for (std::uint32_t index = 1; index < succStart_.size(); ++index) {
        succStart_[index] += succStart_[index - 1u];
    }
    succ_.assign(succStart_.back(), 0);
    auto cursor = succStart_;
    const auto link = [&](std::uint32_t lit, std::uint32_t user) {
        const auto driver = lit >> 1u;
        if (driver <= golden.numInputs) return;
        succ_[cursor[driver]++] = user;
    };
    for (std::uint32_t variable = golden.numInputs + 1u; variable < vars; ++variable) {
        link(in0_[variable], variable);
        link(in1_[variable], variable);
    }

    topo_.clear();
    std::vector<std::uint32_t> ready;
    ready.reserve(vars);
    for (std::uint32_t variable = golden.numInputs + 1u; variable < vars; ++variable) {
        if (indeg_[variable] == 0) ready.push_back(variable);
    }
    while (!ready.empty()) {
        const auto variable = ready.back();
        ready.pop_back();
        topo_.push_back(variable);
        for (auto edge = succStart_[variable]; edge < succStart_[variable + 1u]; ++edge) {
            const auto user = succ_[edge];
            if (--indeg_[user] == 0) ready.push_back(user);
        }
    }
    const auto andCount = vars - golden.numInputs - 1u;
    if (topo_.size() != andCount) throw std::logic_error("rewritten candidate has a cycle");

    repr_.assign(vars, 0);
    fold0_.assign(vars, 0);
    fold1_.assign(vars, 0);
    kept_.assign(vars, 0);
    for (std::uint32_t variable = 0; variable <= golden.numInputs; ++variable) repr_[variable] = variable * 2u;
    for (const auto variable : topo_) {
        const auto left = resolveLit(in0_[variable], repr_);
        const auto right = resolveLit(in1_[variable], repr_);
        if (left == 1u || left == right) {
            repr_[variable] = right;
        } else if (right == 1u) {
            repr_[variable] = left;
        } else if (left != 0u && right != 0u && left != (right ^ 1u)) {
            kept_[variable] = 1;
            fold0_[variable] = left;
            fold1_[variable] = right;
            repr_[variable] = variable * 2u;
        }
    }

    dirty_.assign(nodes, 0);
    CandidateNetlist net;
    net.poDirty.assign(golden.outputs.size(), 0);
    for (std::uint32_t variable = golden.numInputs + 1u; variable < nodes; ++variable) {
        const auto signal = variable == dom ? repr_[wrapper] : repr_[variable];
        const auto sameGate = kept_[variable] && fold0_[variable] == golden.fanin0[variable] &&
                              fold1_[variable] == golden.fanin1[variable];
        if (variable == dest || variable == dom || signal != variable * 2u || !sameGate) {
            dirty_[variable] = 1;
            ++net.dirtyAnds;
        }
    }
    std::vector<std::uint32_t> foldedOutputs(golden.outputs.size());
    for (std::size_t index = 0; index < golden.outputs.size(); ++index) {
        foldedOutputs[index] = resolveLit(structuralOutputs[index], repr_);
        const auto original = golden.outputs[index];
        net.poDirty[index] = foldedOutputs[index] != original ? 1u : 0u;
        if (original > 1u) {
            const auto driver = original >> 1u;
            if (driver > golden.numInputs && driver < nodes && dirty_[driver]) net.poDirty[index] = 1;
        }
    }

    live_.assign(vars, 0);
    std::vector<std::uint32_t> pending;
    const auto mark = [&](std::uint32_t lit) {
        const auto variable = lit >> 1u;
        if (variable <= golden.numInputs || live_[variable]) return;
        live_[variable] = 1;
        pending.push_back(variable);
    };
    for (const auto lit : foldedOutputs) mark(lit);
    while (!pending.empty()) {
        const auto variable = pending.back();
        pending.pop_back();
        if (!kept_[variable]) continue;
        mark(fold0_[variable]);
        mark(fold1_[variable]);
    }

    renumber_.assign(vars, 0);
    for (std::uint32_t variable = 0; variable <= golden.numInputs; ++variable) renumber_[variable] = variable;
    std::uint32_t next = golden.numInputs;
    for (const auto variable : topo_) {
        if (live_[variable] && kept_[variable]) renumber_[variable] = ++next;
    }
    const auto remap = [&](std::uint32_t lit) {
        if (lit <= 1u) return lit;
        return (renumber_[lit >> 1u] << 1u) | (lit & 1u);
    };
    net.n2.numInputs = golden.numInputs;
    net.n2.fanin0.reserve(next - golden.numInputs);
    net.n2.fanin1.reserve(next - golden.numInputs);
    for (const auto variable : topo_) {
        if (!renumber_[variable] || renumber_[variable] <= golden.numInputs) continue;
        net.n2.fanin0.push_back(remap(fold0_[variable]));
        net.n2.fanin1.push_back(remap(fold1_[variable]));
    }
    net.n2.outputs.reserve(foldedOutputs.size());
    for (const auto lit : foldedOutputs) net.n2.outputs.push_back(remap(lit));
#ifndef NDEBUG
    net.n2.check();
#endif
    return net;
}

}  // namespace satrar::rar
