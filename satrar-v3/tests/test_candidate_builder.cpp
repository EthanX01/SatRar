#include "satrar/aig/AagParser.hpp"
#include "satrar/aig/Circuit.hpp"
#include "satrar/analysis/FaultContext.hpp"
#include "satrar/cec/AbcCec.hpp"
#include "satrar/rar/CandidateBuilder.hpp"
#include "satrar/rar/Rewriter.hpp"
#include "satrar/rar/SatRarEngine.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>

using namespace satrar;

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d (%s)\n", __FILE__, __LINE__, #cond);            \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

namespace {

std::filesystem::path labRoot() {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

aig::Circuit referenceOf(const aig::Circuit& circuit, const rar::Alternative& alternative) {
    auto added = rar::Rewriter::addAlternative(circuit, alternative);
    const auto target =
        std::visit([](const auto& item) { return item.attachment.target; }, alternative);
    return rar::Rewriter::removeTarget(std::move(added), target);
}

bool sameBits(const std::vector<bool>& left, const std::vector<bool>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index] != right[index]) return false;
    }
    return true;
}

int compareCandidate(const aig::Circuit& circuit, const rar::GoldenContext& golden,
                     rar::CandidateBuilder& builder, const rar::Alternative& alternative,
                     std::mt19937_64& rng, int patterns) {
    aig::Circuit reference;
    try {
        reference = referenceOf(circuit, alternative);
    } catch (const std::exception&) {
        return 0;
    }
    const auto fast = builder.build(alternative);
    if (reference.stats().andGates != fast.n2.numAnds()) {
        std::fprintf(stderr, "AND count reference=%llu fast=%u\n",
                     static_cast<unsigned long long>(reference.stats().andGates), fast.n2.numAnds());
        return 1;
    }
    const auto inputs = circuit.primaryInputIds().size();
    const auto exhaustive = inputs <= 10;
    const auto total = exhaustive ? (1u << inputs) : static_cast<unsigned>(patterns);
    for (unsigned pattern = 0; pattern < total; ++pattern) {
        std::vector<bool> assignment(inputs, false);
        if (exhaustive) {
            for (std::size_t bit = 0; bit < inputs; ++bit) assignment[bit] = (pattern >> bit) & 1u;
        } else {
            for (std::size_t bit = 0; bit < inputs; ++bit) assignment[bit] = (rng() & 1u) != 0;
        }
        const auto original = circuit.evaluate(assignment);
        const auto expected = reference.evaluate(assignment);
        const auto got = fast.n2.evaluate(assignment);
        const auto goldenBits = golden.net.evaluate(assignment);
        if (!sameBits(original, goldenBits) || !sameBits(expected, got)) return 1;
        for (std::size_t index = 0; index < fast.poDirty.size(); ++index) {
            if (!fast.poDirty[index] && original[index] != got[index]) return 1;
        }
    }
    return 0;
}

std::vector<rar::Alternative> discover(const aig::Circuit& circuit, std::size_t maxTrials) {
    analysis::FaultContext context(circuit.clone());
    rar::SatRarEngine engine(context);
    rar::SatRarOptions options;
    options.mode = rar::SatRarMode::SrarWire;
    options.maxTrials = maxTrials;
    engine.setOptions(options);
    std::vector<rar::Alternative> found;
    for (const auto id : circuit.topologicalOrder()) {
        if (!circuit.nodes()[id].isAnd()) continue;
        for (const auto wire : circuit.faninWires(id)) {
            for (const auto fault : {analysis::StuckAt::Zero, analysis::StuckAt::One}) {
                engine.setPinTarget({wire, fault});
                context.resetSolver();
                auto batch = engine.run();
                found.insert(found.end(), batch.begin(), batch.end());
            }
        }
    }
    return found;
}

aig::Circuit randomCircuit(std::mt19937_64& rng, std::uint32_t inputs, std::uint32_t ands,
                           std::uint32_t outputs) {
    aig::Circuit circuit;
    circuit.addConstantNode(false);
    circuit.addConstantNode(true);
    std::vector<std::uint32_t> literals{0, 1};
    for (std::uint32_t index = 1; index <= inputs; ++index) {
        circuit.addPrimaryInput(index);
        literals.push_back(index * 2u);
        literals.push_back(index * 2u + 1u);
    }
    for (std::uint32_t index = 0; index < ands; ++index) {
        std::uniform_int_distribution<std::size_t> pick(0, literals.size() - 1);
        const auto variable = inputs + 1u + index;
        circuit.addAndGate(variable, aig::Literal(literals[pick(rng)]), aig::Literal(literals[pick(rng)]));
        literals.push_back(variable * 2u);
        literals.push_back(variable * 2u + 1u);
    }
    std::uniform_int_distribution<std::size_t> pick(0, literals.size() - 1);
    for (std::uint32_t index = 0; index < outputs; ++index) {
        circuit.addPrimaryOutput(aig::Literal(literals[pick(rng)]));
    }
    circuit.validate();
    return circuit;
}

int checkFile(const std::filesystem::path& path, int candidateLimit, int patterns) {
    if (!std::filesystem::is_regular_file(path)) return 0;
    const auto circuit = aig::AagParser::parseFile(path.string());
    const auto golden = rar::GoldenContext::build(circuit);
    rar::CandidateBuilder builder(golden);
    std::mt19937_64 rng(11);
    int checked = 0;
    for (const auto& alternative : discover(circuit, 20000)) {
        if (candidateLimit && checked >= candidateLimit) break;
        if (compareCandidate(circuit, *golden, builder, alternative, rng, patterns)) {
            std::fprintf(stderr, "mismatch in %s candidate %d\n", path.string().c_str(), checked);
            return 1;
        }
        ++checked;
    }
    std::fprintf(stderr, "checked %d candidates in %s\n", checked, path.filename().string().c_str());
    return 0;
}

}  // namespace

int main() try {
    const auto root = labRoot();
    const auto self = aig::AagParser::parseFile((root / "satrar-cpp/tests/fixtures/and_self.aag").string());
    const auto andId = self.findNodeByAigVar(2);
    const analysis::PinFault fault{{andId, 0}, analysis::StuckAt::One};
    const rar::WireAlternative hand{{fault, andId, "hand"}, aig::Literal(2)};
    const auto golden = rar::GoldenContext::build(self);
    rar::CandidateBuilder builder(golden);
    std::mt19937_64 rng(1);
    CHECK(compareCandidate(self, *golden, builder, hand, rng, 4) == 0);
    CHECK(checkFile(root / "satrar-cpp/tests/fixtures/halfadder.aag", 0, 16) == 0);

    std::uniform_int_distribution<int> size(6, 14);
    for (int circuitIndex = 0; circuitIndex < 20; ++circuitIndex) {
        const auto inputs = 3u + static_cast<unsigned>(circuitIndex % 4);
        auto circuit = randomCircuit(rng, inputs, static_cast<std::uint32_t>(size(rng)), 2);
        const auto context = rar::GoldenContext::build(circuit);
        rar::CandidateBuilder local(context);
        for (const auto& alternative : discover(circuit, 30)) {
            if (compareCandidate(circuit, *context, local, alternative, rng, 32)) {
                std::fprintf(stderr, "random circuit %d mismatch\n", circuitIndex);
                return 1;
            }
        }
    }

    const auto converted = root / "satrar-v3/results/cec_abc_c432/converted";
    if (std::filesystem::is_directory(converted)) {
        for (const auto& entry : std::filesystem::directory_iterator(converted)) {
            if (entry.path().extension() == ".aag") {
                CHECK(checkFile(entry.path(), 40, 32) == 0);
            }
        }
    }

    cec::AbcCec abc;
    if (!abc.available()) {
        std::fprintf(stderr, "test_candidate_builder: functional ok, ABC injection skipped\n");
        return 0;
    }
    bool injected = false;
    for (int attempt = 0; attempt < 30 && !injected; ++attempt) {
        auto circuit = randomCircuit(rng, 4, 8, 2);
        const auto context = rar::GoldenContext::build(circuit);
        rar::CandidateBuilder local(context);
        for (const auto& alternative : discover(circuit, 40)) {
            auto net = local.build(alternative);
            if (net.n2.numAnds() == 0) continue;
            net.n2.fanin0[0] ^= 1u;
            std::vector<bool> witness;
            bool differs = false;
            for (int pattern = 0; pattern < 64 && !differs; ++pattern) {
                std::vector<bool> assignment(circuit.primaryInputIds().size(), false);
                for (std::size_t bit = 0; bit < assignment.size(); ++bit) assignment[bit] = (rng() & 1u) != 0;
                if (!sameBits(circuit.evaluate(assignment), net.n2.evaluate(assignment))) {
                    witness = assignment;
                    differs = true;
                }
            }
            if (!differs) continue;
            cec::CecJob job;
            job.inputs = context->numInputs;
            job.outputs = static_cast<std::uint32_t>(net.n2.outputs.size());
            job.leftAig = context->aiger;
            job.leftAnds = context->numAnds();
            job.rightAig = std::make_shared<const std::string>(net.n2.toBinaryAiger());
            job.rightAnds = net.n2.numAnds();
            const auto full = std::make_shared<const aig::DenseAig>(net.n2);
            job.replayDifference = [context, full](const std::vector<bool>& assignment) {
                const auto left = context->net.evaluate(assignment);
                const auto right = full->evaluate(assignment);
                for (std::size_t index = 0; index < left.size(); ++index) {
                    if (left[index] != right[index]) return std::optional<std::size_t>(index);
                }
                return std::optional<std::size_t>{};
            };
            const auto answer = abc.checkBatch({job}, 30);
            CHECK(answer.size() == 1);
            CHECK(answer[0].verdict == rar::EquivalenceVerdict::NotEquivalent);
            CHECK(answer[0].counterexampleReplayOk);
            const auto cone = net.n2.selectOutputs(std::vector<std::uint8_t>(net.n2.outputs.size(), 1));
            job.rightAig = std::make_shared<const std::string>(cone.toBinaryAiger());
            job.rightAnds = cone.numAnds();
            const auto coneAnswer = abc.checkBatch({job}, 30);
            CHECK(coneAnswer.size() == 1);
            CHECK(coneAnswer[0].verdict == rar::EquivalenceVerdict::NotEquivalent);
            CHECK(coneAnswer[0].counterexampleReplayOk);
            injected = true;
            break;
        }
    }
    CHECK(injected);
    std::fprintf(stderr, "test_candidate_builder: ok\n");
    return 0;
} catch (const std::exception& ex) {
    std::fprintf(stderr, "FAIL %s\n", ex.what());
    return 1;
}
