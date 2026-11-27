#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace satrar::aig {

/// Combinational AIG in AIGER variable order: 0 is constant 0, 1..numInputs are
/// primary inputs, and the rest are AND gates already stored in topological order.
/// Literals are 2 * variable + polarity, the same encoding as aig::Literal.
struct DenseAig {
    std::uint32_t numInputs = 0;
    std::vector<std::uint32_t> fanin0;
    std::vector<std::uint32_t> fanin1;
    std::vector<std::uint32_t> outputs;

    [[nodiscard]] std::uint32_t numAnds() const noexcept {
        return static_cast<std::uint32_t>(fanin0.size());
    }
    [[nodiscard]] std::uint32_t numVars() const noexcept { return numInputs + 1u + numAnds(); }

    /// Throws if a gate is out of order or a literal is out of range.
    void check() const;

    [[nodiscard]] std::vector<bool> evaluate(const std::vector<bool>& primaryInputs) const;

    /// Keep every primary input. `keepOutput[i] == 0` drops that output and any AND
    /// that no remaining output can reach. Output order follows the surviving outputs.
    [[nodiscard]] DenseAig selectOutputs(const std::vector<std::uint8_t>& keepOutput) const;

    void writeBinaryAiger(std::ostream& out) const;
    [[nodiscard]] std::string toBinaryAiger() const;
};

}  // namespace satrar::aig
