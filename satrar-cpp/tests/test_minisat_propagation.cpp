#include "satrar/sat/MinisatPropagationEngine.hpp"

#include <cstdio>

int main() {
    if (!satrar::sat::MinisatPropagationEngine::runSmokeSelfTest()) {
        std::fprintf(stderr, "Minisat propagation smoke failed\n");
        return 1;
    }
    std::fprintf(stderr, "test_minisat_propagation: ok\n");
    return 0;
}