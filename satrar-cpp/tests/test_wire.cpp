#include "satrar/aig/Circuit.hpp"
#include "satrar/aig/Literal.hpp"
#include "satrar/aig/WireRef.hpp"

#include <iostream>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << " (" #cond ")\n";         \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

int main() {
    using satrar::aig::Circuit;
    using satrar::aig::Literal;
    using satrar::aig::WireRef;

    Circuit c;
    c.addConstantNode(false);
    c.addConstantNode(true);
    c.addPrimaryInput(1);
    c.addPrimaryInput(2);
    const Literal a = Literal::fromVariable(1);
    const Literal b = Literal::fromVariable(2);
    const auto dst = c.addAndGate(3, a, b);
    const auto dst2 = c.addAndGate(4, a, b);
    (void)dst2;

    const WireRef w0{dst, 0};
    const WireRef w0dup{dst, 0};
    const WireRef w1{dst, 1};
    CHECK(w0 == w0dup);
    CHECK(w0 != w1);

    const auto fanout0 = c.fanoutWires(c.findNodeByAigVar(1));
    CHECK(fanout0.size() == 2);
    CHECK(fanout0[0] != fanout0[1]);

    std::cout << "test_wire: ok\n";
    return 0;
}