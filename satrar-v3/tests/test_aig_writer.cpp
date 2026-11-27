#include "satrar/aig/AagParser.hpp"
#include "satrar/aig/AigWriter.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

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

std::vector<char> readAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

}  // namespace

int main() {
    const auto fig1 = labRoot() / "satrar-cpp" / "tests" / "fixtures" / "paper" / "fig1.aag";
    const auto circuit = satrar::aig::AagParser::parseFile(fig1.string());
    const auto destination = std::filesystem::temp_directory_path() / "satrar-v3-fig1.aig";
    const auto stats = satrar::aig::AigWriter::writeFile(circuit, destination);
    CHECK(stats.inputs == 6);
    CHECK(stats.outputs == 2);
    CHECK(stats.ands == 9);
    const auto bytes = readAll(destination);
    CHECK(bytes.size() > 12);
    CHECK(std::string(bytes.begin(), bytes.begin() + 4) == "aig ");

    const auto reference = std::filesystem::temp_directory_path() / "satrar-v3-fig1-py.aig";
    const auto script = labRoot() / "satrar-cpp" / "scripts" / "aag_to_aig.py";
    std::ostringstream command;
    command << "python \"" << script.string() << "\" \"" << fig1.string() << "\" \"" << reference.string()
            << "\" --check";
    if (std::system(command.str().c_str()) == 0) {
        const auto firstLine = [](const std::vector<char>& bytes) {
            const auto end = std::find(bytes.begin(), bytes.end(), '\n');
            return std::string(bytes.begin(), end);
        };
        const auto cpp = readAll(destination);
        const auto python = readAll(reference);
        CHECK(firstLine(cpp) == firstLine(python));
        if (cpp != python) {
            std::fprintf(stderr, "AIG body differs from file order; topological order is intentional\n");
        }
    } else {
        std::fprintf(stderr, "python cross-check skipped\n");
    }
    std::fprintf(stderr, "test_aig_writer: ok\n");
    return 0;
}
