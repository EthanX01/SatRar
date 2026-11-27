#include "satrar/cec/AbcCec.hpp"
#include "satrar/sat/ProcessRunner.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "FAIL %s:%d (%s)\n", __FILE__, __LINE__, #cond);            \
            return 1;                                                                        \
        }                                                                                    \
    } while (0)

int main() {
    const auto abc = satrar::cec::defaultAbcExecutable();
    if (abc.empty() || !std::filesystem::is_regular_file(abc)) {
        std::fprintf(stderr, "test_process_runner: SKIP (ABC unavailable)\n");
        return 77;
    }
    const auto directory = std::filesystem::temp_directory_path() / "satrar-v3-process";
    std::filesystem::create_directories(directory);
    satrar::sat::ProcessRequest request;
    request.executable = abc;
    request.arguments = {"-s", "-c", "quit"};
    request.workingDirectory = directory;
    request.log = directory / "quit.log";
    request.timeoutSeconds = 30;
    const auto quit = satrar::sat::runProcess(request);
    CHECK(quit.started);
    CHECK(quit.finished);
    CHECK(quit.exitCode == 0);

    request.executable = "C:/Windows/System32/ping.exe";
    request.arguments = {"-n", "6", "127.0.0.1"};
    request.log = directory / "ping.log";
    request.timeoutSeconds = 1;
    const auto timed = satrar::sat::runProcess(request);
    CHECK(timed.started);
    CHECK(timed.timedOut);
    CHECK(timed.elapsedSeconds < 5.0);
    std::filesystem::remove_all(directory);
    std::fprintf(stderr, "test_process_runner: ok\n");
    return 0;
}
