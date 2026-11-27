#include "satrar/aig/DenseAig.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>

namespace satrar::aig {
namespace {

void appendDelta(std::string& out, std::uint32_t value) {
    while (value & ~0x7Fu) {
        out.push_back(static_cast<char>((value & 0x7Fu) | 0x80u));
        value >>= 7;
    }
    out.push_back(static_cast<char>(value));
}

std::uint32_t remapLit(std::uint32_t lit, const std::vector<std::uint32_t>& renumber) {
    if (lit <= 1u) return lit;
    const auto variable = lit >> 1u;
    if (variable >= renumber.size() || (variable > 0 && renumber[variable] == 0 && lit > 1u)) {
        throw std::logic_error("dense AIG literal has no renumbered variable");
    }
    return (renumber[variable] << 1u) | (lit & 1u);
}

}  // namespace

void DenseAig::check() const {
    if (fanin0.size() != fanin1.size()) throw std::logic_error("dense AIG fanin width mismatch");
    const auto vars = numVars();
    const auto inRange = [&](std::uint32_t lit) {
        const auto variable = lit >> 1u;
        if (variable >= vars) throw std::logic_error("dense AIG literal out of range");
    };
    for (std::uint32_t index = 0; index < numAnds(); ++index) {
        inRange(fanin0[index]);
        inRange(fanin1[index]);
        const auto lhs = (numInputs + 1u + index) * 2u;
        const auto larger = std::max(fanin0[index], fanin1[index]);
        if (lhs <= larger) throw std::logic_error("dense AIG gate is not in topological order");
    }
    for (const auto lit : outputs) inRange(lit);
}

std::vector<bool> DenseAig::evaluate(const std::vector<bool>& primaryInputs) const {
    if (primaryInputs.size() != numInputs) {
        throw std::invalid_argument("primary-input width does not match the dense AIG");
    }
    std::vector<std::uint8_t> value(numVars(), 0);
    for (std::uint32_t index = 0; index < numInputs; ++index) {
        value[index + 1u] = primaryInputs[index] ? 1u : 0u;
    }
    const auto bit = [&](std::uint32_t lit) {
        return static_cast<std::uint8_t>(value[lit >> 1u] ^ (lit & 1u));
    };
    for (std::uint32_t index = 0; index < numAnds(); ++index) {
        value[numInputs + 1u + index] =
            static_cast<std::uint8_t>(bit(fanin0[index]) & bit(fanin1[index]));
    }
    std::vector<bool> result;
    result.reserve(outputs.size());
    for (const auto lit : outputs) result.push_back(bit(lit) != 0);
    return result;
}

DenseAig DenseAig::selectOutputs(const std::vector<std::uint8_t>& keepOutput) const {
    if (keepOutput.size() != outputs.size()) {
        throw std::invalid_argument("output mask does not match the dense AIG");
    }
    std::vector<std::uint8_t> needed(numVars(), 0);
    std::vector<std::uint32_t> pending;
    const auto push = [&](std::uint32_t lit) {
        if (lit <= 1u) return;
        const auto variable = lit >> 1u;
        if (variable <= numInputs || needed[variable]) return;
        needed[variable] = 1;
        pending.push_back(variable);
    };
    std::vector<std::uint32_t> kept;
    kept.reserve(outputs.size());
    for (std::size_t index = 0; index < outputs.size(); ++index) {
        if (!keepOutput[index]) continue;
        kept.push_back(outputs[index]);
        push(outputs[index]);
    }
    while (!pending.empty()) {
        const auto variable = pending.back();
        pending.pop_back();
        const auto index = variable - numInputs - 1u;
        push(fanin0[index]);
        push(fanin1[index]);
    }

    std::vector<std::uint32_t> renumber(numVars(), 0);
    for (std::uint32_t index = 0; index <= numInputs; ++index) renumber[index] = index;
    DenseAig result;
    result.numInputs = numInputs;
    result.fanin0.reserve(numAnds());
    result.fanin1.reserve(numAnds());
    for (std::uint32_t variable = numInputs + 1u; variable < numVars(); ++variable) {
        if (!needed[variable]) continue;
        renumber[variable] = numInputs + 1u + result.numAnds();
        const auto index = variable - numInputs - 1u;
        result.fanin0.push_back(remapLit(fanin0[index], renumber));
        result.fanin1.push_back(remapLit(fanin1[index], renumber));
    }
    result.outputs.reserve(kept.size());
    for (const auto lit : kept) result.outputs.push_back(remapLit(lit, renumber));
    return result;
}

void DenseAig::writeBinaryAiger(std::ostream& out) const {
    out << toBinaryAiger();
    if (!out) throw std::runtime_error("cannot write binary AIGER");
}

std::string DenseAig::toBinaryAiger() const {
    const auto ands = numAnds();
    const auto maximum = numInputs + ands;
    std::string out;
    out.reserve(static_cast<std::size_t>(ands) * 3u + outputs.size() * 12u + 64u);
    out += "aig " + std::to_string(maximum) + " " + std::to_string(numInputs) + " 0 " +
           std::to_string(outputs.size()) + " " + std::to_string(ands) + "\n";
    for (const auto lit : outputs) out += std::to_string(lit) + "\n";
    for (std::uint32_t index = 0; index < ands; ++index) {
        const auto lhs = (numInputs + 1u + index) * 2u;
        const auto left = fanin0[index];
        const auto right = fanin1[index];
        const auto larger = std::max(left, right);
        const auto smaller = std::min(left, right);
        if (lhs <= larger) throw std::logic_error("dense AIG gate is not in topological order");
        appendDelta(out, lhs - larger);
        appendDelta(out, larger - smaller);
    }
    for (std::uint32_t index = 0; index < numInputs; ++index) {
        out += "i" + std::to_string(index) + " i" + std::to_string(index) + "\n";
    }
    for (std::size_t index = 0; index < outputs.size(); ++index) {
        out += "o" + std::to_string(index) + " o" + std::to_string(index) + "\n";
    }
    out += "c\nsatrar\n";
    return out;
}

}  // namespace satrar::aig
