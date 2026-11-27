#pragma once

#include "satrar/aig/Circuit.hpp"

#include <cstdint>
#include <filesystem>
#include <ostream>

namespace satrar::aig {

struct AigWriteOptions {
    /// Keep only AND gates in the transitive fanin of the primary outputs.
    bool coneOnly = false;
    /// Write i<k> / o<k> names. ABC prints counterexamples with these names.
    bool canonicalSymbols = true;
};

struct AigWriteStats {
    std::uint32_t inputs = 0;
    std::uint32_t outputs = 0;
    std::uint32_t ands = 0;
};

/// Combinational binary AIGER. Every primary input is written, even outside the cone.
/// The byte layout matches scripts/aag_to_aig.py in satrar-cpp.
class AigWriter {
public:
    static AigWriteStats write(const Circuit& circuit, std::ostream& out,
                               const AigWriteOptions& options = {});
    static AigWriteStats writeFile(const Circuit& circuit, const std::filesystem::path& path,
                                   const AigWriteOptions& options = {});
};

}  // namespace satrar::aig
