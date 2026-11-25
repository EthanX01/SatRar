#pragma once

#include "satrar/aig/Literal.hpp"
#include "satrar/aig/Node.hpp"
#include "satrar/aig/WireRef.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace satrar::aig {

struct CircuitStats {
    std::size_t primaryInputs = 0;
    std::size_t primaryOutputs = 0;
    std::size_t andGates = 0;
    std::size_t wires = 0;
    std::uint32_t maxAigVar = 0;
};

class Circuit {
public:
    Circuit() = default;

    [[nodiscard]] bool empty() const noexcept { return nodes_.empty(); }

    [[nodiscard]] const std::vector<Node>& nodes() const noexcept { return nodes_; }
    [[nodiscard]] const std::vector<NodeId>& primaryInputIds() const noexcept { return primaryInputIds_; }
    [[nodiscard]] const std::vector<Literal>& primaryOutputs() const noexcept { return primaryOutputs_; }
    [[nodiscard]] const std::vector<std::optional<std::string>>& outputSymbols() const noexcept { return outputSymbols_; }
    [[nodiscard]] const std::vector<WireRef>& faninWires(NodeId id) const;
    [[nodiscard]] const std::vector<WireRef>& fanoutWires(NodeId id) const;
    [[nodiscard]] std::optional<std::string> comment() const noexcept { return comment_; }

    NodeId constFalseId() const noexcept { return constFalseId_; }
    NodeId constTrueId() const noexcept { return constTrueId_; }

    [[nodiscard]] NodeId findNodeByAigVar(std::uint32_t aigVar) const;
    [[nodiscard]] Literal driverLiteral(WireRef wire) const;

    void validate() const;
    [[nodiscard]] std::vector<NodeId> topologicalOrder() const;
    [[nodiscard]] CircuitStats stats() const;
    [[nodiscard]] Circuit clone() const;
    [[nodiscard]] std::vector<bool> evaluate(const std::vector<bool>& primaryInputValues) const;

    /// Replace one AND input and rebuild fanin/fanout index (rewriter / miter prep).
    void replaceAndFanin(NodeId andId, std::uint8_t pin, Literal lit);

    /// Replace every use of positive/negative literal of \p oldAigVar with \p replacement (PO + AND inputs).
    void substituteAigVar(std::uint32_t oldAigVar, Literal replacement,
                          NodeId except = kInvalidNodeId);

    // Builder API (parser / tests)
    NodeId addConstantNode(bool value);
    NodeId addPrimaryInput(std::uint32_t aigVar, std::optional<std::string> symbol = std::nullopt);
    NodeId addAndGate(std::uint32_t aigVar, Literal left, Literal right);
    void addPrimaryOutput(Literal lit, std::optional<std::string> symbol = std::nullopt);
    void setInputSymbol(std::size_t index, std::string symbol);
    void setOutputSymbol(std::size_t index, std::string symbol);
    void setComment(std::string c);

private:
    void rebuildFaninFanout();

    std::vector<Node> nodes_;
    std::vector<NodeId> primaryInputIds_;
    std::vector<Literal> primaryOutputs_;
    std::vector<std::optional<std::string>> outputSymbols_;
    std::unordered_map<std::uint32_t, NodeId> aigVarToNode_;
    std::vector<std::vector<WireRef>> faninByNode_;
    std::vector<std::vector<WireRef>> fanoutByNode_;
    NodeId constFalseId_ = kInvalidNodeId;
    NodeId constTrueId_ = kInvalidNodeId;
    std::optional<std::string> comment_;

    [[nodiscard]] NodeId resolveDriverNode(Literal lit) const;
};

}  // namespace satrar::aig
