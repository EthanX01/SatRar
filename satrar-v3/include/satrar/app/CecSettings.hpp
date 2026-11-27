#pragma once

#include "satrar/cec/AbcCec.hpp"
#include "satrar/rar/BatchRarVerifier.hpp"

namespace satrar::app {

struct CecSettings {
    cec::AbcOptions abc;
    rar::BatchVerifyOptions batch;
};

}  // namespace satrar::app
