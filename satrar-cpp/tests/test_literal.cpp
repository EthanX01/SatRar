#include "satrar/aig/Literal.hpp"

#include <iostream>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << " (" #cond ")\n";         \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

int main() {
    using satrar::aig::Literal;

    CHECK(Literal(0).isConstant());
    CHECK(Literal(1).isConstant());
    CHECK(Literal(0).valueIfConstant() == false);
    CHECK(Literal(1).valueIfConstant() == true);

    CHECK((~Literal(0)).raw() == 1);
    CHECK((~Literal(1)).raw() == 0);
    CHECK((~Literal(2)).raw() == 3);
    CHECK((~Literal(3)).raw() == 2);

    const Literal v1 = Literal::fromVariable(1, false);
    const Literal nv1 = Literal::fromVariable(1, true);
    CHECK(v1.raw() == 2);
    CHECK(nv1.raw() == 3);
    CHECK(v1.variable() == 1);
    CHECK(nv1.inverted());

    std::cout << "test_literal: ok\n";
    return 0;
}