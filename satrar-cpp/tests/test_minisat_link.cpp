#include "satrar/sat/MinisatPropagationEngine.hpp"

#include <cstdio>

int main() {
    if (!satrar::sat::MinisatPropagationEngine::runSmokeSelfTest()) {
        std::fprintf(stderr, "runSmokeSelfTest failed\n");
        return 1;
    }
    std::fprintf(stderr, "test_minisat_link: ok\n");
    return 0;
}