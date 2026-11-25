#include "satrar/sat/SatBackend.hpp"

namespace satrar::sat {

const char* satVerdictName(SatVerdict v) noexcept {
    switch (v) {
        case SatVerdict::Sat:
            return "SAT";
        case SatVerdict::Unsat:
            return "UNSAT";
        case SatVerdict::Unknown:
            return "UNKNOWN";
        case SatVerdict::Timeout:
            return "TIMEOUT";
        case SatVerdict::Error:
            return "ERROR";
    }
    return "ERROR";
}

}  // namespace satrar::sat
