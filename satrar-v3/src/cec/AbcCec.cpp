#include "satrar/cec/AbcCec.hpp"

#include "satrar/aig/AigWriter.hpp"
#include "satrar/cec/AbcLog.hpp"
#include "satrar/sat/ProcessRunner.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <random>
#include <unordered_map>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace satrar::cec {
namespace {

rar::EquivalenceResult failure(rar::EquivalenceVerdict verdict, const std::string& message) {
    rar::EquivalenceResult result;
    result.verdict = verdict;
    result.message = message;
    return result;
}

class WorkDirectory {
public:
    explicit WorkDirectory(bool keep) : keep_(keep) {
        std::mt19937_64 random{std::random_device{}() ^
                               static_cast<std::uint64_t>(
                                   std::chrono::steady_clock::now().time_since_epoch().count())};
        std::error_code error;
        do {
            path_ = std::filesystem::temp_directory_path() / ("satrar-abc-" + std::to_string(random()));
            error.clear();
        } while (!std::filesystem::create_directory(path_, error));
    }
    ~WorkDirectory() {
        if (keep_) return;
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    WorkDirectory(const WorkDirectory&) = delete;
    WorkDirectory& operator=(const WorkDirectory&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    bool keep_;
};

rar::EquivalenceResult rejected(const std::vector<bool>& assignment, std::size_t output,
                                 const std::string& excerpt) {
    rar::EquivalenceResult result;
    result.verdict = rar::EquivalenceVerdict::NotEquivalent;
    result.counterexamplePi = assignment;
    result.counterexampleReplayOk = true;
    result.differingOutput = output;
    result.message = excerpt;
    return result;
}

rar::EquivalenceResult replay(const CecJob& job, const AbcJobLog& parsed) {
    const auto count = job.replayDifference ? job.inputs
                                            : static_cast<std::uint32_t>(job.left->primaryInputIds().size());
    std::vector<bool> assignment(count, false);
    for (std::size_t index = 0; index < parsed.inputs.size() && index < count; ++index) {
        if (parsed.inputs[index]) assignment[index] = *parsed.inputs[index];
    }
    try {
        if (job.replayDifference) {
            if (const auto output = job.replayDifference(assignment)) {
                return rejected(assignment, *output, parsed.excerpt);
            }
        } else {
            const auto left = job.left->evaluate(assignment);
            const auto right = job.right->evaluate(assignment);
            for (std::size_t index = 0; index < left.size() && index < right.size(); ++index) {
                if (left[index] != right[index]) return rejected(assignment, index, parsed.excerpt);
            }
        }
    } catch (const std::exception& ex) {
        return failure(rar::EquivalenceVerdict::BackendError, ex.what());
    }
    return failure(rar::EquivalenceVerdict::BackendError, "abc_cex_mismatch");
}

struct AigerHead {
    std::uint32_t inputs = 0;
    std::uint32_t outputs = 0;
    std::uint32_t ands = 0;
};

AigerHead readAigerHead(const std::string& bytes) {
    const auto lineEnd = bytes.find('\n');
    if (lineEnd == std::string::npos || bytes.compare(0, 4, "aig ") != 0) {
        throw std::runtime_error("not a binary AIGER blob");
    }
    std::uint32_t fields[5] = {};
    std::size_t cursor = 4;
    for (auto& field : fields) {
        if (cursor >= lineEnd || bytes[cursor] < '0' || bytes[cursor] > '9') {
            throw std::runtime_error("unsupported AIGER header");
        }
        std::uint32_t value = 0;
        while (cursor < lineEnd && bytes[cursor] >= '0' && bytes[cursor] <= '9') {
            value = value * 10u + static_cast<std::uint32_t>(bytes[cursor] - '0');
            ++cursor;
        }
        field = value;
        if (cursor < lineEnd && bytes[cursor] == ' ') ++cursor;
    }
    if (fields[2] != 0) throw std::runtime_error("unsupported AIGER header");
    AigerHead head;
    head.inputs = fields[1];
    head.outputs = fields[3];
    head.ands = fields[4];
    return head;
}

rar::EquivalenceResult fromLog(const CecJob& job, const AbcJobLog* parsed, bool timedOut) {
    if (parsed == nullptr) {
        return failure(timedOut ? rar::EquivalenceVerdict::Timeout : rar::EquivalenceVerdict::BackendError,
                       timedOut ? "solver timeout" : "ABC produced no result");
    }
    switch (parsed->verdict) {
        case AbcVerdict::Equivalent:
            return failure(rar::EquivalenceVerdict::Equivalent, "ABC equivalent");
        case AbcVerdict::NotEquivalent:
            return replay(job, *parsed);
        case AbcVerdict::Undecided:
            return failure(rar::EquivalenceVerdict::Unknown, parsed->excerpt);
        case AbcVerdict::Missing:
            return failure(timedOut ? rar::EquivalenceVerdict::Timeout : rar::EquivalenceVerdict::BackendError,
                           timedOut ? "solver timeout" : "ABC produced no result");
        case AbcVerdict::Error:
            return failure(rar::EquivalenceVerdict::BackendError,
                           parsed->excerpt.empty() ? "ABC error" : parsed->excerpt);
    }
    return failure(rar::EquivalenceVerdict::Error, parsed->excerpt);
}

std::filesystem::path siblingAbc() {
#ifdef _WIN32
    std::wstring module(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (size == 0 || size >= module.size()) return {};
    module.resize(size);
    const auto directory = std::filesystem::path(module).parent_path();
    for (const wchar_t* name : {L"yosys-abc.exe", L"abc.exe"}) {
        const auto candidate = directory / name;
        if (std::filesystem::is_regular_file(candidate)) return candidate;
    }
#else
    (void)0;
#endif
    return {};
}

}  // namespace

std::filesystem::path defaultAbcExecutable() {
    if (const char* configured = std::getenv("SATRAR_ABC_EXE"); configured != nullptr && *configured != '\0') {
        return configured;
    }
#ifdef SATRAR_CONFIGURED_ABC
    if (std::filesystem::is_regular_file(SATRAR_CONFIGURED_ABC)) return SATRAR_CONFIGURED_ABC;
#endif
    if (const auto sibling = siblingAbc(); !sibling.empty()) return sibling;
    return {};
}

AbcCec::AbcCec(AbcOptions options) : options_(std::move(options)) {
    if (options_.executable.empty()) options_.executable = defaultAbcExecutable();
}

bool AbcCec::available() const {
    std::error_code error;
    return std::filesystem::is_regular_file(options_.executable, error);
}

std::string AbcCec::describe() const { return "ABC: " + options_.executable.string(); }

std::vector<rar::EquivalenceResult> AbcCec::runOnce(const std::vector<CecJob>& jobs,
                                                    std::uint32_t timeoutSeconds,
                                                    CecBatchStats* stats) const {
    std::vector<rar::EquivalenceResult> results(jobs.size(), failure(rar::EquivalenceVerdict::Error, "not run"));
    if (!available()) {
        for (auto& result : results) result = failure(rar::EquivalenceVerdict::BackendError, "abc unavailable");
        return results;
    }
    WorkDirectory directory(options_.keepFiles);
    struct LeftFile {
        std::string name;
        std::uint32_t ands = 0;
    };
    std::unordered_map<const void*, LeftFile> leftFiles;
    std::vector<AbcScriptEntry> entries;
    std::vector<std::size_t> submitted;
    const auto writeStarted = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < jobs.size(); ++index) {
        const auto& job = jobs[index];
        const bool bytes = job.leftAig && job.rightAig;
        if (!bytes && (!job.left || !job.right)) {
            results[index] = failure(rar::EquivalenceVerdict::InvalidInput, "missing circuit");
            continue;
        }
        try {
            if (bytes) {
                const auto leftHead = readAigerHead(*job.leftAig);
                const auto rightHead = readAigerHead(*job.rightAig);
                if (leftHead.inputs != rightHead.inputs || leftHead.outputs != rightHead.outputs) {
                    results[index] = failure(rar::EquivalenceVerdict::InvalidInput, "circuit interfaces differ");
                    continue;
                }
            } else if (job.left->primaryInputIds().size() != job.right->primaryInputIds().size() ||
                       job.left->primaryOutputs().size() != job.right->primaryOutputs().size()) {
                results[index] = failure(rar::EquivalenceVerdict::InvalidInput, "circuit interfaces differ");
                continue;
            }
            const void* leftKey = bytes ? static_cast<const void*>(job.leftAig.get())
                                        : static_cast<const void*>(job.left.get());
            auto& leftFile = leftFiles[leftKey];
            if (leftFile.name.empty()) {
                leftFile.name = "left_" + std::to_string(leftFiles.size()) + ".aig";
                const auto path = directory.path() / leftFile.name;
                if (bytes) {
                    std::ofstream file(path, std::ios::binary);
                    file << *job.leftAig;
                    if (!file) throw std::runtime_error("cannot write left AIGER");
                    leftFile.ands = job.leftAnds ? job.leftAnds : readAigerHead(*job.leftAig).ands;
                } else {
                    leftFile.ands = aig::AigWriter::writeFile(*job.left, path).ands;
                }
            }
            const auto rightName = "cand_" + std::to_string(job.id) + ".aig";
            std::uint32_t rightAnds = job.rightAnds;
            if (bytes) {
                std::ofstream file(directory.path() / rightName, std::ios::binary);
                file << *job.rightAig;
                if (!file) throw std::runtime_error("cannot write candidate AIGER");
                if (rightAnds == 0) rightAnds = readAigerHead(*job.rightAig).ands;
            } else {
                rightAnds = aig::AigWriter::writeFile(*job.right, directory.path() / rightName).ands;
            }
            if (stats) stats->coneAnds += static_cast<std::uint64_t>(leftFile.ands) + rightAnds;
            entries.push_back({job.id, leftFile.name, rightName});
            submitted.push_back(index);
        } catch (const std::exception& ex) {
            results[index] = failure(rar::EquivalenceVerdict::InvalidInput, ex.what());
        }
    }
    if (stats) {
        stats->writeNs += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - writeStarted)
                .count());
    }
    if (entries.empty()) return results;

    {
        std::ofstream script(directory.path() / "script.abc", std::ios::binary);
        script << buildAbcScript(entries, std::max<std::uint32_t>(timeoutSeconds, 1));
        if (!script) {
            for (const auto index : submitted) {
                results[index] = failure(rar::EquivalenceVerdict::BackendError, "cannot write ABC script");
            }
            return results;
        }
    }
    const auto cap = std::max<std::uint32_t>(options_.batchTimeoutCap, 1);
    const auto wall = std::min<std::uint64_t>(
        static_cast<std::uint64_t>(timeoutSeconds) * entries.size() + 10ull, cap);
    sat::ProcessRequest request;
    request.executable = options_.executable;
    request.arguments = {"-s", "-f", "script.abc"};
    request.workingDirectory = directory.path();
    request.log = directory.path() / "abc.log";
    request.timeoutSeconds = static_cast<std::uint32_t>(std::max<std::uint64_t>(wall, 1));
    const auto abcStarted = std::chrono::steady_clock::now();
    const auto process = sat::runProcess(request);
    if (stats) {
        stats->abcNs += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - abcStarted)
                .count());
    }
    std::ifstream log(request.log);
    const auto parsed = parseAbcLog(log);
    const double share = process.elapsedSeconds / static_cast<double>(submitted.size());
    std::vector<std::size_t> retry;
    for (const auto index : submitted) {
        const auto found = parsed.jobs.find(jobs[index].id);
        const AbcJobLog* item = found == parsed.jobs.end() ? nullptr : &found->second;
        auto result = fromLog(jobs[index], item, process.timedOut);
        result.elapsedSeconds = share;
        if (process.timedOut && result.verdict == rar::EquivalenceVerdict::Timeout) retry.push_back(index);
        results[index] = std::move(result);
    }
    if (options_.keepFiles) {
        for (auto& result : results) {
            if (!result.message.empty()) result.message += " ";
            result.message += directory.path().string();
        }
    }
    for (const auto index : retry) {
        if (submitted.size() == 1) continue;
        CecJob alone = jobs[index];
        auto again = runOnce({alone}, timeoutSeconds, stats);
        if (!again.empty()) results[index] = std::move(again[0]);
    }
    return results;
}

std::vector<rar::EquivalenceResult> AbcCec::checkBatch(const std::vector<CecJob>& jobs,
                                                       std::uint32_t timeoutSeconds,
                                                       CecBatchStats* stats) const {
    return runOnce(jobs, timeoutSeconds, stats);
}

}  // namespace satrar::cec
