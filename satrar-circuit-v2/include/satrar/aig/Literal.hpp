#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace satrar::aig {

/// AIGER wire literal: \c raw = 2 * variable + inversion_bit.
/// Constants: 0 = false, 1 = true. Not to be confused with DIMACS signed literals.
class Literal final {
public:
    static constexpr std::uint32_t kConstFalse = 0;
    static constexpr std::uint32_t kConstTrue = 1;

    constexpr Literal() noexcept : raw_(kConstFalse) {}

    explicit constexpr Literal(std::uint32_t raw) noexcept : raw_(raw) {}

    static Literal fromVariable(std::uint32_t variable, bool inverted = false) {
        if (variable == 0) {
            throw std::invalid_argument("AIG variable index must be >= 1 (0 is reserved for constants)");
        }
        return Literal((variable << 1) | static_cast<std::uint32_t>(inverted));
    }

    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return raw_; }

    [[nodiscard]] constexpr bool isConstant() const noexcept { return raw_ <= kConstTrue; }

    [[nodiscard]] constexpr bool inverted() const noexcept { return (raw_ & 1u) != 0u; }

    [[nodiscard]] std::uint32_t variable() const {
        if (isConstant()) {
            throw std::logic_error("constants have no AIG variable index");
        }
        return raw_ >> 1u;
    }

    [[nodiscard]] constexpr Literal operator~() const noexcept { return Literal(raw_ ^ 1u); }

    [[nodiscard]] constexpr bool valueIfConstant() const {
        if (!isConstant()) {
            throw std::logic_error("literal is not a constant");
        }
        return raw_ == kConstTrue;
    }

    [[nodiscard]] constexpr bool operator==(Literal other) const noexcept { return raw_ == other.raw_; }
    [[nodiscard]] constexpr bool operator!=(Literal other) const noexcept { return raw_ != other.raw_; }

    [[nodiscard]] std::string toString() const;

private:
    std::uint32_t raw_;
};

}  // namespace satrar::aig