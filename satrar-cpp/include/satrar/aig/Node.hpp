#pragma once

#include "satrar/aig/Literal.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace satrar::aig {

using NodeId = std::uint32_t;
constexpr NodeId kInvalidNodeId = UINT32_MAX;

enum class NodeKind { Constant, PrimaryInput, And };

struct AndInputs {
    Literal left;
    Literal right;
};

struct Node {
    NodeKind kind = NodeKind::Constant;
    std::uint32_t aagVar = 0;  /// 0 for const nodes; else AIGER variable index
    std::optional<std::string> symbol;
    std::variant<std::monostate, AndInputs> data;

    [[nodiscard]] bool isConstant() const noexcept { return kind == NodeKind::Constant; }
    [[nodiscard]] bool isPrimaryInput() const noexcept { return kind == NodeKind::PrimaryInput; }
    [[nodiscard]] bool isAnd() const noexcept { return kind == NodeKind::And; }

    [[nodiscard]] const AndInputs& andInputs() const {
        return std::get<AndInputs>(data);
    }
};

}  // namespace satrar::aig