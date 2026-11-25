#include "minisat/core/Solver.h"
#include "minisat/mtl/Vec.h"

#include <iostream>

int main() {
    Minisat::Solver solver;
    Minisat::vec<Minisat::Lit> clause;
    clause.push(Minisat::mkLit(0, false));
    clause.push(Minisat::mkLit(1, true));
    if (!solver.addClause(clause)) {
        std::cerr << "addClause failed\n";
        return 1;
    }
    std::cout << "test_minisat_bare: ok\n";
    return 0;
}