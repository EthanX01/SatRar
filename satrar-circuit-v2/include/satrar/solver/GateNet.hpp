#pragma once

#include "satrar/aig/Circuit.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace satrar::solver {

/// Immutable AND-inverter net compiled once from a circuit.
/// Dense signal 0 is constant false. Every PI and AND output gets one dense id.
/// Several SolverSession objects may share one GateNet.
class GateNet {
public:
    GateNet() = default;
    explicit GateNet(const aig::Circuit& circuit);

    [[nodiscard]] std::uint32_t signalCount() const noexcept {
        return static_cast<std::uint32_t>(aigOf_.size());
    }
    [[nodiscard]] std::uint32_t gateCount() const noexcept {
        return static_cast<std::uint32_t>(gates_.size());
    }
    [[nodiscard]] const std::vector<std::uint32_t>& primaryInputs() const noexcept { return pis_; }

    [[nodiscard]] std::uint32_t aigVar(std::uint32_t dense) const { return aigOf_.at(dense); }
    [[nodiscard]] bool isPrimaryInput(std::uint32_t dense) const { return isPi_.at(dense); }
    [[nodiscard]] std::uint32_t denseOfAig(std::uint32_t aigVar) const;

    struct Edge {
        std::uint32_t var = 0;
        bool negated = false;
    };
    struct Gate {
        std::uint32_t out = 0;
        Edge left;
        Edge right;
        aig::NodeId node = aig::kInvalidNodeId;
        std::uint32_t aigVar = 0;
    };

    [[nodiscard]] const Gate& gate(std::uint32_t index) const { return gates_.at(index); }
    [[nodiscard]] const std::vector<std::uint32_t>& incident(std::uint32_t dense) const {
        return incident_.at(dense);
    }

    [[nodiscard]] aig::Literal literalOf(std::uint32_t dense, bool negated) const;
    [[nodiscard]] Edge pack(aig::Literal lit) const;

    /// Gates visited while the incident lists were built (one per endpoint registration).
    [[nodiscard]] std::size_t indexTouches() const noexcept { return indexTouches_; }

private:
    std::vector<std::uint32_t> aigOf_;
    std::vector<char> isPi_;
    std::vector<std::uint32_t> pis_;
    std::unordered_map<std::uint32_t, std::uint32_t> denseOf_;
    std::vector<Gate> gates_;
    std::vector<std::vector<std::uint32_t>> incident_;
    std::size_t indexTouches_ = 0;
};

}  // namespace satrar::solver
