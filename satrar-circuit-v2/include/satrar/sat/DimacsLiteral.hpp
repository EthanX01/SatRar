#pragma once

#include <cstdint>

namespace satrar::sat {

/// DIMACS literal (signed integer). Not the same type as AIGER literals.
using DimacsLiteral = std::int32_t;

inline DimacsLiteral dimacsFromVar(std::uint32_t var, bool positive = true) {
    return static_cast<DimacsLiteral>(positive ? static_cast<int>(var) : -static_cast<int>(var));
}

inline std::uint32_t dimacsVariable(DimacsLiteral lit) {
    return static_cast<std::uint32_t>(lit > 0 ? lit : -lit);
}

inline bool dimacsSignPositive(DimacsLiteral lit) { return lit > 0; }

}  // namespace satrar::sat