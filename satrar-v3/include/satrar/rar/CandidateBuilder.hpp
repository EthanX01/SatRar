#pragma once

#include "satrar/rar/Alternative.hpp"
#include "satrar/rar/GoldenContext.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace satrar::rar {

/// N2 for one candidate: the golden circuit with the alternative added, the target
/// pin tied to its stuck-at value, constants folded, and unreachable gates removed.
struct CandidateNetlist {
    aig::DenseAig n2;
    /// 1 when that primary output's function may differ from golden.
    std::vector<std::uint8_t> poDirty;
    std::uint32_t dirtyAnds = 0;
};

/// One forward fold of the golden netlist. Scratch buffers stay with the builder
/// so a worker can reuse them across candidates. Does not recheck dominators.
class CandidateBuilder {
public:
    explicit CandidateBuilder(std::shared_ptr<const GoldenContext> golden);
    [[nodiscard]] CandidateNetlist build(const Alternative& alternative);

private:
    std::shared_ptr<const GoldenContext> golden_;
    std::vector<std::uint32_t> in0_;
    std::vector<std::uint32_t> in1_;
    std::vector<std::uint32_t> repr_;
    std::vector<std::uint32_t> fold0_;
    std::vector<std::uint32_t> fold1_;
    std::vector<std::uint8_t> kept_;
    std::vector<std::uint8_t> live_;
    std::vector<std::uint32_t> indeg_;
    std::vector<std::uint32_t> succStart_;
    std::vector<std::uint32_t> succ_;
    std::vector<std::uint32_t> topo_;
    std::vector<std::uint32_t> renumber_;
    std::vector<std::uint32_t> dirty_;
};

}  // namespace satrar::rar
