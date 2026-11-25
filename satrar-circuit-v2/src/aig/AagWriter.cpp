#include "satrar/aig/AagWriter.hpp"

#include <fstream>
#include <stdexcept>

namespace satrar::aig {

void AagWriter::write(const Circuit& circuit, std::ostream& out) {
    circuit.validate();
    const CircuitStats st = circuit.stats();
    out << "aag " << st.maxAigVar << ' ' << st.primaryInputs << " 0 " << st.primaryOutputs << ' '
        << st.andGates << '\n';

    for (const NodeId id : circuit.primaryInputIds()) {
        const std::uint32_t v = circuit.nodes()[id].aagVar;
        out << (v << 1u) << '\n';
    }
    for (const Literal po : circuit.primaryOutputs()) {
        out << po.raw() << '\n';
    }

    const auto order = circuit.topologicalOrder();
    for (const NodeId id : order) {
        const Node& node = circuit.nodes()[id];
        if (!node.isAnd()) {
            continue;
        }
        const auto& in = node.andInputs();
        out << (node.aagVar << 1u) << ' ' << in.left.raw() << ' ' << in.right.raw() << '\n';
    }

    for (std::size_t i = 0; i < circuit.primaryInputIds().size(); ++i) {
        const auto& symbol = circuit.nodes()[circuit.primaryInputIds()[i]].symbol;
        if (symbol) {
            out << 'i' << i << ' ' << *symbol << '\n';
        }
    }
    for (std::size_t i = 0; i < circuit.outputSymbols().size(); ++i) {
        if (const auto& symbol = circuit.outputSymbols()[i]) {
            out << 'o' << i << ' ' << *symbol << '\n';
        }
    }
    if (circuit.comment()) {
        out << "c\n" << *circuit.comment() << '\n';
    }
}

void AagWriter::writeFile(const Circuit& circuit, const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot write '" + path + "'");
    }
    write(circuit, file);
    file.close();
    if (!file) {
        throw std::runtime_error("cannot finish writing '" + path + "'");
    }
}

}  // namespace satrar::aig
