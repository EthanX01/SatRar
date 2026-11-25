
#include "satrar/sat/MiterBuilder.hpp"

#include "satrar/sat/CnfConstants.hpp"

namespace satrar::sat {

std::uint32_t MiterBuilder::primaryInputEncodingKey(std::size_t index) {
    if (index >= 0xE0000000u) {
        throw std::overflow_error("too many miter inputs");
    }
    return static_cast<std::uint32_t>(index);
}

MiterBuilder::EncodedCircuit MiterBuilder::encode(const aig::Circuit& circuit, MiterSide,
                                                 CnfFormula* appendInto) {
    circuit.validate();
    EncodedCircuit result;
    auto& formula = appendInto ? *appendInto : result.cnf;
    const auto constants = ensureAigConstants(formula);
    for (std::size_t i = 0; i < circuit.primaryInputIds().size(); ++i) {
        const auto var = circuit.nodes()[circuit.primaryInputIds()[i]].aagVar;
        result.signals.emplace(var, dimacsFromVar(formula.ensureAigVar(primaryInputEncodingKey(i)), true));
    }

    const auto literal = [&](aig::Literal edge) {
        if (edge.isConstant()) {
            return aigLiteralToDimacs(edge, constants, formula);
        }
        const auto value = result.signals.at(edge.variable());
        return edge.inverted() ? -value : value;
    };
    for (const auto id : circuit.topologicalOrder()) {
        const auto& node = circuit.nodes()[id];
        if (node.isAnd()) {
            const auto z = dimacsFromVar(formula.newVariable(), true);
            const auto& input = node.andInputs();
            const auto x = literal(input.left);
            const auto y = literal(input.right);
            result.signals.emplace(node.aagVar, z);
            formula.addClause({-z, x});
            formula.addClause({-z, y});
            formula.addClause({z, -x, -y});
        }
    }
    for (const auto po : circuit.primaryOutputs()) {
        result.primaryOutputLits.push_back(literal(po));
    }
    return result;
}

DimacsLiteral MiterBuilder::buildXorMiter(const EncodedCircuit& golden,
                                         const EncodedCircuit& candidate, CnfFormula& formula) {
    if (golden.primaryOutputLits.size() != candidate.primaryOutputLits.size()) {
        throw std::invalid_argument("miter: PO count mismatch");
    }
    std::vector<DimacsLiteral> differences;
    for (std::size_t i = 0; i < golden.primaryOutputLits.size(); ++i) {
        const auto x = golden.primaryOutputLits[i];
        const auto y = candidate.primaryOutputLits[i];
        const auto z = dimacsFromVar(formula.newVariable(), true);
        formula.addClause({-z, -x, -y});
        formula.addClause({-z, x, y});
        formula.addClause({z, -x, y});
        formula.addClause({z, x, -y});
        differences.push_back(z);
    }
    const auto any = dimacsFromVar(formula.newVariable(), true);
    for (const auto difference : differences) {
        formula.addClause({-difference, any});
    }
    differences.push_back(-any);
    formula.addClause(std::move(differences));
    formula.addClause({any});
    return any;
}

CnfFormula MiterBuilder::buildNotEquivalentFormula(const aig::Circuit& golden,
                                                   const aig::Circuit& candidate) {
    if (golden.primaryInputIds().size() != candidate.primaryInputIds().size()) {
        throw std::invalid_argument("miter: PI count mismatch");
    }
    CnfFormula formula;
    const auto a = encode(golden, MiterSide::A, &formula);
    const auto b = encode(candidate, MiterSide::B, &formula);
    (void)buildXorMiter(a, b, formula);
    return formula;
}

}  // namespace satrar::sat
