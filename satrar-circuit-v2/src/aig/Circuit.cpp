#include "satrar/aig/Circuit.hpp"

#include <algorithm>
#include <queue>
#include <sstream>
#include <unordered_set>

namespace satrar::aig {

namespace {

void throwValidation(const std::string& msg) {
    throw std::runtime_error(msg);
}

}  // namespace

const std::vector<WireRef>& Circuit::faninWires(NodeId id) const {
    if (id >= faninByNode_.size()) {
        static const std::vector<WireRef> empty;
        return empty;
    }
    return faninByNode_[id];
}

const std::vector<WireRef>& Circuit::fanoutWires(NodeId id) const {
    if (id >= fanoutByNode_.size()) {
        static const std::vector<WireRef> empty;
        return empty;
    }
    return fanoutByNode_[id];
}

NodeId Circuit::findNodeByAigVar(std::uint32_t aigVar) const {
    const auto it = aigVarToNode_.find(aigVar);
    if (it == aigVarToNode_.end()) {
        return kInvalidNodeId;
    }
    return it->second;
}

NodeId Circuit::resolveDriverNode(Literal lit) const {
    if (lit.isConstant()) {
        return lit.valueIfConstant() ? constTrueId_ : constFalseId_;
    }
    return findNodeByAigVar(lit.variable());
}

Literal Circuit::driverLiteral(WireRef wire) const {
    if (!wire.valid() || wire.destination >= nodes_.size()) {
        throw std::out_of_range("invalid wire");
    }
    const Node& dst = nodes_[wire.destination];
    if (!dst.isAnd()) {
        throw std::logic_error("driver literal requested for non-AND pin");
    }
    const auto& in = dst.andInputs();
    return wire.inputPin == 0 ? in.left : in.right;
}

NodeId Circuit::addConstantNode(bool value) {
  const NodeId id = static_cast<NodeId>(nodes_.size());
    Node n;
    n.kind = NodeKind::Constant;
    n.aagVar = 0;
    nodes_.push_back(std::move(n));
    if (fanoutByNode_.size() < nodes_.size()) {
        fanoutByNode_.resize(nodes_.size());
    }
    if (value) {
        constTrueId_ = id;
    } else {
        constFalseId_ = id;
    }
    return id;
}

NodeId Circuit::addPrimaryInput(std::uint32_t aigVar, std::optional<std::string> symbol) {
    if (aigVar == 0) {
        throwValidation("primary input AIG variable must be >= 1");
    }
    if (aigVarToNode_.count(aigVar) != 0) {
        throwValidation("duplicate primary input variable");
    }
    const NodeId id = static_cast<NodeId>(nodes_.size());
    Node n;
    n.kind = NodeKind::PrimaryInput;
    n.aagVar = aigVar;
    n.symbol = std::move(symbol);
    nodes_.push_back(std::move(n));
    aigVarToNode_[aigVar] = id;
    primaryInputIds_.push_back(id);
    if (fanoutByNode_.size() < nodes_.size()) {
        fanoutByNode_.resize(nodes_.size());
    }
    return id;
}

NodeId Circuit::addAndGate(std::uint32_t aigVar, Literal left, Literal right) {
    if (aigVar == 0) {
        throwValidation("AND gate variable must be >= 1");
    }
    if (aigVarToNode_.count(aigVar) != 0) {
        throwValidation("duplicate AND variable");
    }
    const NodeId id = static_cast<NodeId>(nodes_.size());
    Node n;
    n.kind = NodeKind::And;
    n.aagVar = aigVar;
    n.data = AndInputs{left, right};
    nodes_.push_back(std::move(n));
    aigVarToNode_[aigVar] = id;
    if (faninByNode_.size() < nodes_.size()) {
        faninByNode_.resize(nodes_.size());
        fanoutByNode_.resize(nodes_.size());
    }
    const WireRef w0{id, 0};
    const WireRef w1{id, 1};
    faninByNode_[id] = {w0, w1};
    const NodeId leftDrv = resolveDriverNode(left);
    const NodeId rightDrv = resolveDriverNode(right);
    if (leftDrv != kInvalidNodeId) {
        fanoutByNode_[leftDrv].push_back(w0);
    }
    if (rightDrv != kInvalidNodeId) {
        fanoutByNode_[rightDrv].push_back(w1);
    }
    return id;
}

void Circuit::addPrimaryOutput(Literal lit, std::optional<std::string> symbol) {
    primaryOutputs_.push_back(lit);
    outputSymbols_.push_back(std::move(symbol));
}

void Circuit::setInputSymbol(std::size_t index, std::string symbol) {
    nodes_.at(primaryInputIds_.at(index)).symbol = std::move(symbol);
}

void Circuit::setOutputSymbol(std::size_t index, std::string symbol) {
    outputSymbols_.at(index) = std::move(symbol);
}

void Circuit::setComment(std::string c) {
    comment_ = std::move(c);
}

namespace {

Literal substituteVar(Literal use, std::uint32_t oldVar, Literal replacement) {
    if (use.isConstant()) {
        return use;
    }
    if (use.variable() != oldVar) {
        return use;
    }
    return use.inverted() ? ~replacement : replacement;
}

}  // namespace

void Circuit::substituteAigVar(std::uint32_t oldAigVar, Literal replacement, NodeId except) {
    for (NodeId id = 0; id < nodes_.size(); ++id) {
        Node& node = nodes_[id];
        if (id == except || !node.isAnd()) {
            continue;
        }
        auto inputs = node.andInputs();
        inputs.left = substituteVar(inputs.left, oldAigVar, replacement);
        inputs.right = substituteVar(inputs.right, oldAigVar, replacement);
        node.data = inputs;
    }
    for (Literal& po : primaryOutputs_) {
        po = substituteVar(po, oldAigVar, replacement);
    }
    rebuildFaninFanout();
}

void Circuit::replaceAndFanin(NodeId andId, std::uint8_t pin, Literal lit) {
    if (andId >= nodes_.size() || !nodes_[andId].isAnd()) {
        throwValidation("replaceAndFanin: not an AND node");
    }
    if (pin > 1) {
        throwValidation("replaceAndFanin: pin must be 0 or 1");
    }
    AndInputs in = nodes_[andId].andInputs();
    if (pin == 0) {
        in.left = lit;
    } else {
        in.right = lit;
    }
    nodes_[andId].data = in;
    rebuildFaninFanout();
}

void Circuit::rebuildFaninFanout() {
    faninByNode_.assign(nodes_.size(), {});
    fanoutByNode_.assign(nodes_.size(), {});

    for (NodeId dst = 0; dst < nodes_.size(); ++dst) {
        const Node& node = nodes_[dst];
        if (!node.isAnd()) {
            continue;
        }
        const auto& in = node.andInputs();
        const NodeId leftDrv = resolveDriverNode(in.left);
        const NodeId rightDrv = resolveDriverNode(in.right);
        WireRef w0{dst, 0};
        WireRef w1{dst, 1};
        faninByNode_[dst] = {w0, w1};
        if (leftDrv != kInvalidNodeId) {
            fanoutByNode_[leftDrv].push_back(w0);
        }
        if (rightDrv != kInvalidNodeId) {
            fanoutByNode_[rightDrv].push_back(w1);
        }
    }
}

void Circuit::validate() const {
    if (constFalseId_ == kInvalidNodeId || constTrueId_ == kInvalidNodeId) {
        throwValidation("circuit missing constant nodes");
    }
    for (const NodeId pi : primaryInputIds_) {
        if (pi >= nodes_.size() || !nodes_[pi].isPrimaryInput()) {
            throwValidation("invalid primary input index");
        }
    }
    for (const Node& node : nodes_) {
        if (!node.isAnd()) {
            continue;
        }
        const auto& in = node.andInputs();
        auto checkFanin = [this](Literal lit) {
            if (lit.isConstant()) {
                return;
            }
            const std::uint32_t v = lit.variable();
            if (findNodeByAigVar(v) == kInvalidNodeId) {
                std::ostringstream os;
                os << "undefined fanin variable " << v;
                throwValidation(os.str());
            }
        };
        checkFanin(in.left);
        checkFanin(in.right);
    }

    for (const auto output : primaryOutputs_) {
        if (resolveDriverNode(output) == kInvalidNodeId) {
            throwValidation("undefined primary output variable");
        }
    }
    const auto order = topologicalOrder();
    if (order.size() != nodes_.size()) {
        throwValidation("combinational cycle detected");
    }
}

std::vector<NodeId> Circuit::topologicalOrder() const {
    std::vector<std::size_t> indeg(nodes_.size(), 0);
    std::vector<std::vector<NodeId>> adj(nodes_.size());
    for (NodeId dst = 0; dst < nodes_.size(); ++dst) {
        if (!nodes_[dst].isAnd()) {
            continue;
        }
        for (const WireRef& w : faninByNode_[dst]) {
            const Literal lit = driverLiteral(w);
            const NodeId src = resolveDriverNode(lit);
            if (src == kInvalidNodeId) {
                continue;
            }
            adj[src].push_back(dst);
            ++indeg[dst];
        }
    }

    std::queue<NodeId> q;
    for (NodeId i = 0; i < nodes_.size(); ++i) {
        if (indeg[i] == 0) {
            q.push(i);
        }
    }

    std::vector<NodeId> order;
    order.reserve(nodes_.size());
    while (!q.empty()) {
        const NodeId u = q.front();
        q.pop();
        order.push_back(u);
        for (const NodeId v : adj[u]) {
            if (--indeg[v] == 0) {
                q.push(v);
            }
        }
    }
    if (order.size() != nodes_.size()) {
        return {};
    }
    return order;
}

namespace {

bool literalValue(Literal lit, const std::unordered_map<std::uint32_t, bool>& signal) {
    if (lit.isConstant()) {
        return lit.valueIfConstant();
    }
    const auto it = signal.find(lit.variable());
    if (it == signal.end()) {
        throw std::runtime_error("missing value for AIG variable");
    }
    const bool v = it->second;
    return lit.inverted() ? !v : v;
}

}  // namespace

std::vector<bool> Circuit::evaluate(const std::vector<bool>& primaryInputValues) const {
    if (primaryInputValues.size() != primaryInputIds_.size()) {
        throwValidation("primary input assignment size mismatch");
    }

    std::unordered_map<std::uint32_t, bool> signal;
    for (std::size_t i = 0; i < primaryInputIds_.size(); ++i) {
        signal[nodes_[primaryInputIds_[i]].aagVar] = primaryInputValues[i];
    }

    const auto order = topologicalOrder();
    if (order.empty()) {
        throwValidation("cannot evaluate: cyclic or invalid circuit");
    }

    for (const NodeId id : order) {
        const Node& n = nodes_[id];
        if (!n.isAnd()) {
            continue;
        }
        const auto& in = n.andInputs();
        const bool l = literalValue(in.left, signal);
        const bool r = literalValue(in.right, signal);
        signal[n.aagVar] = l && r;
    }

    std::vector<bool> outputs;
    outputs.reserve(primaryOutputs_.size());
    for (const Literal po : primaryOutputs_) {
        outputs.push_back(literalValue(po, signal));
    }
    return outputs;
}

CircuitStats Circuit::stats() const {
    CircuitStats s;
    s.primaryInputs = primaryInputIds_.size();
    s.primaryOutputs = primaryOutputs_.size();
    s.maxAigVar = 0;
    for (const Node& n : nodes_) {
        if (n.isAnd()) {
            ++s.andGates;
        }
        s.maxAigVar = std::max(s.maxAigVar, n.aagVar);
    }
    s.wires = 0;
    for (const Node& n : nodes_) {
        if (n.isAnd()) {
            s.wires += 2;
        }
    }
    return s;
}

Circuit Circuit::clone() const {
    Circuit c;
    c.nodes_ = nodes_;
    c.primaryInputIds_ = primaryInputIds_;
    c.primaryOutputs_ = primaryOutputs_;
    c.outputSymbols_ = outputSymbols_;
    c.aigVarToNode_ = aigVarToNode_;
    c.constFalseId_ = constFalseId_;
    c.constTrueId_ = constTrueId_;
    c.comment_ = comment_;
    c.rebuildFaninFanout();
    return c;
}

}  // namespace satrar::aig
