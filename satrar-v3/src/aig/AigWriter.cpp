#include "satrar/aig/AigWriter.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace satrar::aig {
namespace {

void putDelta(std::ostream& out, std::uint32_t value) {
    while (value & ~0x7Fu) {
        out.put(static_cast<char>((value & 0x7Fu) | 0x80u));
        value >>= 7;
    }
    out.put(static_cast<char>(value));
}

std::unordered_set<std::uint32_t> outputCone(const Circuit& circuit) {
    std::unordered_set<std::uint32_t> cone;
    const auto mark = [&](auto&& self, Literal lit) -> void {
        if (lit.isConstant()) return;
        if (!cone.insert(lit.variable()).second) return;
        const auto id = circuit.findNodeByAigVar(lit.variable());
        if (id == kInvalidNodeId || !circuit.nodes()[id].isAnd()) return;
        const auto& inputs = circuit.nodes()[id].andInputs();
        self(self, inputs.left);
        self(self, inputs.right);
    };
    for (const Literal output : circuit.primaryOutputs()) mark(mark, output);
    return cone;
}

}  // namespace

AigWriteStats AigWriter::write(const Circuit& circuit, std::ostream& out, const AigWriteOptions& options) {
    circuit.validate();
    const auto cone = options.coneOnly ? outputCone(circuit) : std::unordered_set<std::uint32_t>{};
    std::unordered_map<std::uint32_t, std::uint32_t> newVar;
    const auto inputs = static_cast<std::uint32_t>(circuit.primaryInputIds().size());
    for (std::uint32_t index = 0; index < inputs; ++index) {
        newVar.emplace(circuit.nodes()[circuit.primaryInputIds()[index]].aagVar, index + 1);
    }
    const auto mapped = [&](Literal lit) {
        if (lit.isConstant()) return lit.raw();
        const auto found = newVar.find(lit.variable());
        if (found == newVar.end()) {
            throw std::runtime_error("AIG literal is used before its gate is defined");
        }
        return (found->second << 1u) | (lit.inverted() ? 1u : 0u);
    };

    struct Encoded {
        std::uint32_t lhs = 0;
        std::uint32_t larger = 0;
        std::uint32_t smaller = 0;
    };
    // Topological order, not AIGER-variable order. A rewrite can number a new gate
    // above a gate that already uses it, so sorting by variable is not safe.
    std::vector<Encoded> gates;
    std::uint32_t next = inputs + 1;
    for (const NodeId id : circuit.topologicalOrder()) {
        const Node& node = circuit.nodes()[id];
        if (!node.isAnd()) continue;
        if (options.coneOnly && cone.count(node.aagVar) == 0) continue;
        const auto& in = node.andInputs();
        const auto left = mapped(in.left);
        const auto right = mapped(in.right);
        newVar.emplace(node.aagVar, next);
        const auto larger = std::max(left, right);
        const auto smaller = std::min(left, right);
        const auto lhs = next * 2u;
        if (lhs <= larger) {
            throw std::runtime_error("AND gate is not in topological order");
        }
        gates.push_back({lhs, larger, smaller});
        ++next;
    }
    std::vector<std::uint32_t> outputs;
    outputs.reserve(circuit.primaryOutputs().size());
    for (const Literal output : circuit.primaryOutputs()) outputs.push_back(mapped(output));

    const auto ands = static_cast<std::uint32_t>(gates.size());
    const auto maximum = inputs + ands;
    out << "aig " << maximum << ' ' << inputs << " 0 " << outputs.size() << ' ' << ands << '\n';
    for (const auto lit : outputs) out << lit << '\n';
    for (const auto& gate : gates) {
        putDelta(out, gate.lhs - gate.larger);
        putDelta(out, gate.larger - gate.smaller);
    }
    if (options.canonicalSymbols) {
        for (std::uint32_t index = 0; index < inputs; ++index) {
            out << 'i' << index << " i" << index << '\n';
        }
        for (std::size_t index = 0; index < outputs.size(); ++index) {
            out << 'o' << index << " o" << index << '\n';
        }
    }
    out << "c\nsatrar\n";
    if (!out) throw std::runtime_error("cannot write binary AIGER");
    return {inputs, static_cast<std::uint32_t>(outputs.size()), ands};
}

AigWriteStats AigWriter::writeFile(const Circuit& circuit, const std::filesystem::path& path,
                                   const AigWriteOptions& options) {
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open '" + path.string() + "'");
    const auto stats = write(circuit, file, options);
    file.close();
    if (!file) throw std::runtime_error("cannot finish '" + path.string() + "'");
    return stats;
}

}  // namespace satrar::aig
