#include "satrar/aig/AagParser.hpp"
#include "satrar/aig/Circuit.hpp"
#include "satrar/analysis/GraphAnalysis.hpp"

#include <cstdio>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d (%s)\n", __FILE__, __LINE__, #cond);            \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

namespace {

struct InvariantReport {
    std::string path;
    std::size_t andGates = 0;
    std::size_t naiveCycleBlocksOnExistingFanin = 0;
    std::size_t sameAndSiblings = 0;
    std::size_t reconvergentAnds = 0;
};

std::vector<std::filesystem::path> collectAagPaths(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> out;
    const std::filesystem::path dirs[] = {
        root / "tests" / "fixtures",
        root / "benchmarks" / "inputs" / "aiger-examples",
        root / "benchmarks" / "inputs" / "epfl-random_control",
        root / "benchmarks" / "inputs" / "epfl-arithmetic",
    };
    for (const std::filesystem::path& d : dirs) {
        if (!std::filesystem::is_directory(d)) {
            continue;
        }
        for (const auto& ent : std::filesystem::directory_iterator(d)) {
            if (ent.path().extension() == ".aag") {
                out.push_back(ent.path());
            }
        }
    }
    return out;
}

bool checkFaninFanoutSymmetric(const satrar::aig::Circuit& circuit) {
    for (satrar::aig::NodeId id = 0; id < circuit.nodes().size(); ++id) {
        for (const satrar::aig::WireRef& w : circuit.faninWires(id)) {
            if (w.destination != id) {
                return false;
            }
            bool inFanout = false;
            const satrar::aig::Literal lit = circuit.driverLiteral(w);
            if (lit.isConstant()) {
                continue;
            }
            const satrar::aig::NodeId drv = circuit.findNodeByAigVar(lit.variable());
            if (drv == satrar::aig::kInvalidNodeId) {
                return false;
            }
            for (const satrar::aig::WireRef& fo : circuit.fanoutWires(drv)) {
                if (fo.destination == w.destination && fo.inputPin == w.inputPin) {
                    inFanout = true;
                    break;
                }
            }
            if (!inFanout) {
                return false;
            }
        }
    }
    return true;
}

InvariantReport auditCircuit(const std::filesystem::path& path) {
    InvariantReport rep;
    rep.path = path.string();
    const auto circuit = satrar::aig::AagParser::parseFile(path.string());
    circuit.validate();
    if (!checkFaninFanoutSymmetric(circuit)) {
        throw std::runtime_error("fanin/fanout asymmetric");
    }

    const auto order = circuit.topologicalOrder();
    if (order.size() != circuit.nodes().size()) {
        throw std::runtime_error("topological order size mismatch");
    }

    const satrar::aig::Circuit cloned = circuit.clone();
    cloned.validate();
    if (!checkFaninFanoutSymmetric(cloned)) {
        throw std::runtime_error("clone fanin/fanout asymmetric");
    }

    satrar::analysis::GraphAnalysis graph(circuit);

    std::size_t andCount = 0;
    for (satrar::aig::NodeId id = 0; id < circuit.nodes().size(); ++id) {
        if (circuit.nodes()[id].isAnd()) {
            ++andCount;
        }
    }
    rep.andGates = andCount;
    const bool fullSiblingAudit = andCount <= 5000;

    for (satrar::aig::NodeId id = 0; id < circuit.nodes().size(); ++id) {
        if (!circuit.nodes()[id].isAnd()) {
            continue;
        }
        const auto& fins = circuit.faninWires(id);
        if (fins.size() < 2) {
            continue;
        }
        const satrar::aig::Literal l0 = circuit.driverLiteral(fins[0]);
        const satrar::aig::Literal l1 = circuit.driverLiteral(fins[1]);
        if (!l0.isConstant() && !l1.isConstant() && l0.variable() == l1.variable() &&
            l0.inverted() == l1.inverted()) {
            ++rep.reconvergentAnds;
        }

        if (!fullSiblingAudit) {
            continue;
        }
        for (const satrar::aig::WireRef& faultWire : fins) {
            for (const satrar::aig::WireRef& sib : fins) {
                if (sib == faultWire) {
                    continue;
                }
                ++rep.sameAndSiblings;
                if (graph.wouldBypassWireCreateCycle(faultWire, sib)) {
                    throw std::runtime_error("same-AND sibling bypass flagged as cycle");
                }

                const satrar::aig::Literal sdrv = circuit.driverLiteral(sib);
                satrar::aig::NodeId driverId = satrar::aig::kInvalidNodeId;
                if (sdrv.isConstant()) {
                    driverId = sdrv.valueIfConstant() ? circuit.constTrueId() : circuit.constFalseId();
                } else {
                    driverId = circuit.findNodeByAigVar(sdrv.variable());
                }
                if (driverId != satrar::aig::kInvalidNodeId &&
                    graph.wouldCreateCycle(driverId, id)) {
                    ++rep.naiveCycleBlocksOnExistingFanin;
                }
            }
        }
    }
    return rep;
}

}  // namespace

int main() {
    const std::filesystem::path root =
        std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::vector<std::filesystem::path> aags = collectAagPaths(root);
    CHECK(!aags.empty());

    std::size_t parsed = 0;
    std::size_t skipped = 0;
    std::size_t totalNaiveFp = 0;
    std::size_t totalReconvergent = 0;

    const std::filesystem::path reportPath = root / "results" / "aag_invariant_report.tsv";
    std::filesystem::create_directories(root / "results");
    std::ofstream report(reportPath);
    CHECK(static_cast<bool>(report));
    report << "path\tandGates\treconvergentAnds\tsameAndSiblings\tnaiveCycleFalsePositives\n";

    for (const std::filesystem::path& path : aags) {
        try {
            const InvariantReport rep = auditCircuit(path);
            ++parsed;
            totalNaiveFp += rep.naiveCycleBlocksOnExistingFanin;
            totalReconvergent += rep.reconvergentAnds;
            report << rep.path << '\t' << rep.andGates << '\t' << rep.reconvergentAnds << '\t'
                   << rep.sameAndSiblings << '\t' << rep.naiveCycleBlocksOnExistingFanin << '\n';
        } catch (const std::exception& ex) {
            if (std::string(ex.what()).find("latches") != std::string::npos ||
                std::string(ex.what()).find("combinational-only") != std::string::npos) {
                ++skipped;
                continue;
            }
            std::fprintf(stderr, "FAIL parse %s: %s\n", path.string().c_str(), ex.what());
            return 1;
        }
    }

    report.close();
    std::fprintf(stderr,
                 "test_aag_invariants: ok parsed=%zu skipped=%zu totalNaiveCycleFP=%zu "
                 "reconvergentANDs=%zu report=%s\n",
                 parsed, skipped, totalNaiveFp, totalReconvergent, reportPath.string().c_str());
    std::fprintf(stderr,
                 "note: naiveCycleFalsePositives counts old wouldCreateCycle(driver,and) on "
                 "existing fanins — expected >>0; bypass API must stay false for same-AND siblings.\n");
    return 0;
}