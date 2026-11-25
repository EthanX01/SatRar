#include "satrar/aig/Literal.hpp"

#include <sstream>

namespace satrar::aig {

std::string Literal::toString() const {
    if (isConstant()) {
        return valueIfConstant() ? "const1" : "const0";
    }
    std::ostringstream os;
    os << (inverted() ? '~' : ' ') << 'v' << variable();
    return os.str();
}

}  // namespace satrar::aig