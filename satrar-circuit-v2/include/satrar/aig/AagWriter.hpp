#pragma once

#include "satrar/aig/Circuit.hpp"

#include <ostream>
#include <string>

namespace satrar::aig {

/// Write combinational ASCII AIGER (.aag) from an in-memory circuit.
class AagWriter {
public:
    static void write(const Circuit& circuit, std::ostream& out);
    static void writeFile(const Circuit& circuit, const std::string& path);
};

}  // namespace satrar::aig