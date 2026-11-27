#pragma once

#include "satrar/aig/Circuit.hpp"
#include "satrar/aig/DenseAig.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace satrar::rar {

/// One golden circuit, numbered once: 0 is constant 0, 1..numInputs are primary
/// inputs in circuit order, and the rest are AND gates in topological order.
struct GoldenContext {
    std::uint32_t numInputs = 0;
    /// Indexed by dense variable. Entries 0..numInputs are unused.
    std::vector<std::uint32_t> fanin0;
    std::vector<std::uint32_t> fanin1;
    std::vector<std::uint32_t> outputs;
    std::vector<std::uint32_t> denseOfNode;
    std::vector<aig::NodeId> nodeOfDense;
    std::vector<std::uint32_t> denseOfAigVar;
    /// CSR: AND variables that read this variable.
    std::vector<std::uint32_t> foStart;
    std::vector<std::uint32_t> foList;
    /// CSR: primary-output positions that read this variable directly.
    std::vector<std::uint32_t> poStart;
    std::vector<std::uint32_t> poList;
    aig::DenseAig net;
    std::shared_ptr<const std::string> aiger;

    [[nodiscard]] std::uint32_t numNodes() const noexcept {
        return static_cast<std::uint32_t>(nodeOfDense.size());
    }
    [[nodiscard]] std::uint32_t numAnds() const noexcept { return numNodes() - numInputs - 1u; }

    [[nodiscard]] static std::shared_ptr<const GoldenContext> build(const aig::Circuit& golden);
    [[nodiscard]] std::uint32_t denseLit(aig::Literal lit) const;
};

}  // namespace satrar::rar
