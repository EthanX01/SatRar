
#include "satrar/rar/ExportAag.hpp"

#include "satrar/aig/AagParser.hpp"
#include "satrar/aig/AagWriter.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <random>
#include <sstream>

namespace satrar::rar {
namespace {

std::filesystem::path temporaryFor(const std::filesystem::path& destination) {
    static std::atomic<std::uint64_t> counter{0};
    std::mt19937_64 random(std::random_device{}() ^
                           static_cast<std::uint64_t>(
                               std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto parent = destination.has_parent_path() ? destination.parent_path()
                                                      : std::filesystem::path(".");
    return parent / (destination.filename().string() + ".tmp" + std::to_string(random()) + "-" +
                     std::to_string(counter++));
}

}  // namespace

ExportResult exportVerifiedAag(const aig::Circuit& original, const aig::Circuit& rewritten,
                               const std::filesystem::path& destination,
                               const sat::SatBackend& backend, const ExportOptions& options) {
    ExportResult result;
    const auto temporary = temporaryFor(destination);
    const auto cleanup = [&] {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    };
    try {
        std::ostringstream text;
        aig::AagWriter::write(rewritten, text);
        {
            std::ofstream file(temporary, std::ios::binary);
            if (!file) {
                throw std::runtime_error("cannot create temporary AAG '" + temporary.string() + "'");
            }
            file << text.str();
            file.flush();
            if (!file) {
                throw std::runtime_error("cannot write temporary AAG");
            }
            file.close();
            if (file.fail()) {
                throw std::runtime_error("cannot close temporary AAG");
            }
        }
        // The CEC input is what a later reader of the published file would see.
        const auto reloaded = aig::AagParser::parseFile(temporary.string());
        reloaded.validate();
        const auto check = EquivalenceChecker(backend).check(original, reloaded, options.timeoutSeconds);
        result.roundTrip = check.verdict;
        if (check.verdict != EquivalenceVerdict::Equivalent) {
            result.message = std::string("export round-trip CEC ") +
                             equivalenceVerdictName(check.verdict) + ": " + check.message;
            cleanup();
            return result;
        }
        if (options.beforePublish) {
            options.beforePublish(temporary);
        }
        std::filesystem::rename(temporary, destination);
        result.published = true;
        result.message = "published";
        return result;
    } catch (const std::exception& ex) {
        cleanup();
        result.published = false;
        result.message = std::string("export failed: ") + ex.what();
        return result;
    }
}

}  // namespace satrar::rar
