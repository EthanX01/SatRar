#include "satrar/rar/GoldenContext.hpp"

#include <algorithm>
#include <stdexcept>

namespace satrar::rar {

std::uint32_t GoldenContext::denseLit(aig::Literal lit) const {
    if (lit.isConstant()) return lit.raw();
    const auto variable = lit.variable();
    if (variable >= denseOfAigVar.size() || denseOfAigVar[variable] == aig::kInvalidNodeId) {
        throw std::invalid_argument("literal is not in the golden circuit");
    }
    return (denseOfAigVar[variable] << 1u) | (lit.inverted() ? 1u : 0u);
}

std::shared_ptr<const GoldenContext> GoldenContext::build(const aig::Circuit& golden) {
    golden.validate();
    auto context = std::make_shared<GoldenContext>();
    const auto inputCount = static_cast<std::uint32_t>(golden.primaryInputIds().size());
    const auto andCount = static_cast<std::uint32_t>(golden.stats().andGates);
    const auto nodes = inputCount + andCount + 1u;
    context->numInputs = inputCount;
    context->fanin0.assign(nodes, 0);
    context->fanin1.assign(nodes, 0);
    context->denseOfNode.assign(golden.nodes().size(), aig::kInvalidNodeId);
    context->nodeOfDense.assign(nodes, aig::kInvalidNodeId);
    context->nodeOfDense[0] = golden.constFalseId();
    if (golden.constFalseId() < context->denseOfNode.size()) {
        context->denseOfNode[golden.constFalseId()] = 0;
    }

    std::uint32_t maxVar = 0;
    for (const auto& node : golden.nodes()) maxVar = std::max(maxVar, node.aagVar);
    context->denseOfAigVar.assign(static_cast<std::size_t>(maxVar) + 1u, aig::kInvalidNodeId);

    for (std::uint32_t index = 0; index < inputCount; ++index) {
        const auto id = golden.primaryInputIds()[index];
        const auto dense = index + 1u;
        context->denseOfNode[id] = dense;
        context->nodeOfDense[dense] = id;
        context->denseOfAigVar[golden.nodes()[id].aagVar] = dense;
    }

    std::uint32_t next = inputCount;
    for (const auto id : golden.topologicalOrder()) {
        const auto& node = golden.nodes()[id];
        if (!node.isAnd()) continue;
        const auto dense = ++next;
        context->denseOfNode[id] = dense;
        context->nodeOfDense[dense] = id;
        context->denseOfAigVar[node.aagVar] = dense;
        const auto& inputs = node.andInputs();
        context->fanin0[dense] = context->denseLit(inputs.left);
        context->fanin1[dense] = context->denseLit(inputs.right);
    }
    if (next + 1u != nodes) throw std::logic_error("golden AND count does not match the topology");

    context->outputs.reserve(golden.primaryOutputs().size());
    for (const auto output : golden.primaryOutputs()) context->outputs.push_back(context->denseLit(output));

    context->foStart.assign(static_cast<std::size_t>(nodes) + 1u, 0);
    for (std::uint32_t andVar = inputCount + 1u; andVar < nodes; ++andVar) {
        for (const auto lit : {context->fanin0[andVar], context->fanin1[andVar]}) {
            ++context->foStart[(lit >> 1u) + 1u];
        }
    }
    for (std::uint32_t index = 1; index < context->foStart.size(); ++index) {
        context->foStart[index] += context->foStart[index - 1u];
    }
    context->foList.resize(context->foStart.back());
    auto cursor = context->foStart;
    for (std::uint32_t andVar = inputCount + 1u; andVar < nodes; ++andVar) {
        for (const auto lit : {context->fanin0[andVar], context->fanin1[andVar]}) {
            context->foList[cursor[lit >> 1u]++] = andVar;
        }
    }

    context->poStart.assign(static_cast<std::size_t>(nodes) + 1u, 0);
    for (std::uint32_t index = 0; index < context->outputs.size(); ++index) {
        ++context->poStart[(context->outputs[index] >> 1u) + 1u];
    }
    for (std::uint32_t index = 1; index < context->poStart.size(); ++index) {
        context->poStart[index] += context->poStart[index - 1u];
    }
    context->poList.resize(context->poStart.back());
    cursor = context->poStart;
    for (std::uint32_t index = 0; index < context->outputs.size(); ++index) {
        context->poList[cursor[context->outputs[index] >> 1u]++] = index;
    }

    context->net.numInputs = inputCount;
    context->net.fanin0.assign(context->fanin0.begin() + static_cast<std::ptrdiff_t>(inputCount) + 1,
                               context->fanin0.end());
    context->net.fanin1.assign(context->fanin1.begin() + static_cast<std::ptrdiff_t>(inputCount) + 1,
                               context->fanin1.end());
    context->net.outputs = context->outputs;
    context->net.check();
    context->aiger = std::make_shared<const std::string>(context->net.toBinaryAiger());
    return context;
}

}  // namespace satrar::rar
