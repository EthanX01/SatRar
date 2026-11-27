#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace satrar::sat {

struct ProcessRequest {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path workingDirectory;
    std::filesystem::path log;
    std::uint32_t timeoutSeconds = 300;
};

struct ProcessResult {
    bool started = false;
    bool finished = false;
    bool timedOut = false;
    int exitCode = -1;
    double elapsedSeconds = 0.0;
    std::string message;
};

[[nodiscard]] ProcessResult runProcess(const ProcessRequest& request);

}  // namespace satrar::sat
