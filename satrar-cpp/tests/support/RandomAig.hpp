// Seeded random combinational AIGs for differential tests. Includes constant pins, duplicated and
// opposite-signed fanins, shared fanouts, dead gates and multiple primary outputs.
#pragma once

#include "satrar/aig/Circuit.hpp"

#include <algorithm>
#include <cstdint>

namespace randomaig {

struct Rng {
    std::uint64_t state;
    std::uint64_t next() {
        std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    unsigned below(unsigned n) { return static_cast<unsigned>(next() % n); }
};

inline satrar::aig::Circuit randomCircuit(Rng& rng) {
    using satrar::aig::Literal;
    satrar::aig::Circuit c;
    c.addConstantNode(false);
    c.addConstantNode(true);
    const unsigned pis = 3 + rng.below(4);
    const unsigned ands = 4 + rng.below(12);
    for (unsigned i = 1; i <= pis; ++i) c.addPrimaryInput(i);
    const auto pick = [&](unsigned limit) {
        if (rng.below(14) == 0) return Literal(rng.below(2));  // constant pin
        const auto var = 1 + rng.below(limit);
        return Literal(2 * var + rng.below(2));
    };
    for (unsigned i = 0; i < ands; ++i) {
        const auto var = pis + 1 + i;
        auto left = pick(var - 1);
        auto right = rng.below(8) == 0 ? (rng.below(2) ? left : ~left) : pick(var - 1);
        c.addAndGate(var, left, right);
    }
    const unsigned total = pis + ands;
    const unsigned outputs = 1 + rng.below(3);
    for (unsigned i = 0; i < outputs; ++i) {
        const auto var = rng.below(3) ? total - rng.below(std::min(total, 4u)) : 1 + rng.below(total);
        c.addPrimaryOutput(Literal(2 * var + rng.below(2)));
    }
    c.validate();
    return c;
}

}  // namespace randomaig
