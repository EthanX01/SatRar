#include "satrar/cec/AbcLog.hpp"

#include <cctype>
#include <regex>
#include <sstream>

namespace satrar::cec {
namespace {

std::string lower(std::string text) {
    for (char& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

bool startsWith(const std::string& text, const char* prefix) {
    const auto size = std::char_traits<char>::length(prefix);
    return text.size() >= size && text.compare(0, size, prefix) == 0;
}

bool parseMarker(const std::string& line, const char* prefix, std::uint64_t& id) {
    if (!startsWith(line, prefix)) return false;
    try {
        std::size_t used = 0;
        id = std::stoull(line.substr(std::char_traits<char>::length(prefix)), &used);
        return used > 0 && used == line.size() - std::char_traits<char>::length(prefix);
    } catch (...) {
        return false;
    }
}

AbcJobLog classify(const std::vector<std::string>& lines) {
    AbcJobLog job;
    int conclusions = 0;
    bool error = false;
    AbcVerdict verdict = AbcVerdict::Undecided;
    std::ostringstream excerpt;
    const std::regex input("i(\\d+)\\s*=\\s*1'h([01])");
    for (const auto& line : lines) {
        if (job.excerpt.size() < 400) {
            excerpt << line << '\n';
            job.excerpt = excerpt.str().substr(0, 400);
        }
        const auto folded = lower(line);
        if (folded.find("networks are not equivalent") != std::string::npos) {
            ++conclusions;
            verdict = AbcVerdict::NotEquivalent;
        } else if (folded.find("networks are equivalent") != std::string::npos) {
            ++conclusions;
            verdict = AbcVerdict::Equivalent;
        } else if (folded.find("undecided") != std::string::npos) {
            ++conclusions;
            verdict = AbcVerdict::Undecided;
        }
        if (folded.find("error:") != std::string::npos || folded.find("cannot open") != std::string::npos) {
            error = true;
        }
        for (std::sregex_iterator match(line.begin(), line.end(), input), done; match != done; ++match) {
            const auto index = std::stoul((*match)[1].str());
            const bool value = (*match)[2].str() == "1";
            if (job.inputs.size() <= index) job.inputs.resize(index + 1);
            if (job.inputs[index] && *job.inputs[index] != value) error = true;
            job.inputs[index] = value;
        }
    }
    if (error || conclusions != 1) {
        job.verdict = conclusions == 0 && !error ? AbcVerdict::Undecided : AbcVerdict::Error;
        if (job.verdict != AbcVerdict::NotEquivalent) job.inputs.clear();
        return job;
    }
    job.verdict = verdict;
    if (verdict != AbcVerdict::NotEquivalent) job.inputs.clear();
    return job;
}

}  // namespace

const char* abcVerdictName(AbcVerdict verdict) noexcept {
    switch (verdict) {
        case AbcVerdict::Equivalent: return "equivalent";
        case AbcVerdict::NotEquivalent: return "not_equivalent";
        case AbcVerdict::Undecided: return "undecided";
        case AbcVerdict::Error: return "error";
        case AbcVerdict::Missing: return "missing";
    }
    return "error";
}

std::string buildAbcScript(const std::vector<AbcScriptEntry>& entries, std::uint32_t timeoutSeconds) {
    std::ostringstream script;
    for (const auto& entry : entries) {
        script << "echo SATRAR_BEGIN " << entry.id << '\n'
               << "&cec -T " << timeoutSeconds << ' ' << entry.left << ' ' << entry.right << '\n'
               << "echo SATRAR_END " << entry.id << '\n';
    }
    script << "quit\n";
    return script.str();
}

AbcLogParse parseAbcLog(std::istream& log) {
    AbcLogParse parsed;
    std::optional<std::uint64_t> current;
    std::vector<std::string> block;
    std::string line;
    const auto abandon = [&] {
        if (!current) return;
        parsed.jobs[*current] = AbcJobLog{};
        current.reset();
        block.clear();
    };
    while (std::getline(log, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto text = trim(line);
        std::uint64_t id = 0;
        if (parseMarker(text, "SATRAR_BEGIN ", id)) {
            if (current) parsed.malformed = true;
            abandon();
            current = id;
            continue;
        }
        if (parseMarker(text, "SATRAR_END ", id)) {
            if (!current || *current != id) {
                parsed.malformed = true;
                abandon();
                continue;
            }
            parsed.jobs.emplace(id, classify(block));
            current.reset();
            block.clear();
            continue;
        }
        if (current) block.push_back(text);
    }
    if (current) {
        parsed.malformed = true;
        abandon();
    }
    return parsed;
}

}  // namespace satrar::cec
