#include "satrar/sat/CnfEncoder.hpp"

#include "satrar/sat/CnfConstants.hpp"

namespace satrar::sat {

namespace {

void addAndTseitin(CnfFormula& formula, DimacsLiteral z, DimacsLiteral x, DimacsLiteral y) {
    formula.addClause({-z, x});
    formula.addClause({-z, y});
    formula.addClause({z, -x, -y});
}

}  // namespace

CnfFormula CnfEncoder::encodeCircuit(const aig::Circuit& circuit) {
    CnfFormula formula;
    const CnfConstVars constants = bindAigConstants(formula);

    for (const aig::NodeId id : circuit.primaryInputIds()) {
        (void)formula.ensureAigVar(circuit.nodes()[id].aagVar);
    }

    const auto order = circuit.topologicalOrder();
    for (const aig::NodeId id : order) {
        const aig::Node& node = circuit.nodes()[id];
        if (node.isAnd()) {
            (void)formula.ensureAigVar(node.aagVar);
        }
    }

    for (const aig::NodeId id : order) {
        const aig::Node& node = circuit.nodes()[id];
        if (!node.isAnd()) {
            continue;
        }
        const std::uint32_t outSat = formula.satVarOfAig(node.aagVar);
        const DimacsLiteral z = dimacsFromVar(outSat, true);
        const auto& in = node.andInputs();
        addAndTseitin(formula, z, aigLiteralToDimacs(in.left, constants, formula),
                      aigLiteralToDimacs(in.right, constants, formula));
    }

    return formula;
}

std::optional<DimacsLiteral> CnfEncoder::positiveLit(const CnfFormula& formula, std::uint32_t aigVar) {
    try {
        return dimacsFromVar(formula.satVarOfAig(aigVar), true);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<DimacsLiteral> CnfEncoder::dimacsLitForAig(const CnfFormula& formula, aig::Literal lit) {
    CnfConstVars constants{};
    try {
        constants.falseVar = formula.satVarOfAig(kCnfConstFalseKey);
        constants.trueVar = formula.satVarOfAig(kCnfConstTrueKey);
    } catch (...) {
        return std::nullopt;
    }
    if (lit.isConstant()) {
        return aigLiteralToDimacs(lit, constants, formula);
    }
    try {
        return aigLiteralToDimacs(lit, constants, formula);
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace satrar::sat