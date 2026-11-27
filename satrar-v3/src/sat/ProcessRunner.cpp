#include "satrar/sat/ProcessRunner.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#endif

namespace satrar::sat {
namespace {

using Clock = std::chrono::steady_clock;

void rejectQuote(const std::string& text) {
    if (text.find('"') != std::string::npos) {
        throw std::invalid_argument("process arguments cannot contain quotes");
    }
}

#ifdef _WIN32
std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const auto size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                          nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}
#endif

}  // namespace

ProcessResult runProcess(const ProcessRequest& request) {
    ProcessResult result;
    const auto startedAt = Clock::now();
    const auto finish = [&] {
        result.elapsedSeconds = std::chrono::duration<double>(Clock::now() - startedAt).count();
        return result;
    };
    try {
        rejectQuote(request.executable.string());
        for (const auto& argument : request.arguments) rejectQuote(argument);
    } catch (const std::exception& ex) {
        result.message = ex.what();
        return finish();
    }

#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    const auto output = CreateFileW(request.log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    const auto input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
        if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
        if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
        result.message = "cannot open process log";
        return finish();
    }
    STARTUPINFOW info{};
    info.cb = sizeof(info);
    info.dwFlags = STARTF_USESTDHANDLES;
    info.hStdInput = input;
    info.hStdOutput = info.hStdError = output;
    std::wstring command = L"\"" + request.executable.wstring() + L"\"";
    for (const auto& argument : request.arguments) {
        command += L" \"" + widen(argument) + L"\"";
    }
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');
    const wchar_t* directory = request.workingDirectory.empty() ? nullptr : request.workingDirectory.c_str();
    PROCESS_INFORMATION process{};
    const auto started = CreateProcessW(request.executable.c_str(), buffer.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, directory, &info, &process);
    CloseHandle(output);
    CloseHandle(input);
    if (!started) {
        result.message = "process launch failed: Windows error " + std::to_string(GetLastError());
        return finish();
    }
    result.started = true;
    const auto wait = WaitForSingleObject(
        process.hProcess, static_cast<DWORD>(std::min<std::uint64_t>(request.timeoutSeconds * 1000ull, 0xFFFFFFFEull)));
    if (wait == WAIT_OBJECT_0) {
        DWORD status = 0;
        if (GetExitCodeProcess(process.hProcess, &status)) result.exitCode = static_cast<int>(status);
        result.finished = true;
    } else if (wait == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
        result.timedOut = true;
        result.message = "process timeout";
    } else {
        result.message = "process wait failed";
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return finish();
#else
    const int fd = open(request.log.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0) {
        result.message = "cannot open process log";
        return finish();
    }
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(request.executable.c_str()));
    std::vector<std::string> storage = request.arguments;
    for (auto& argument : storage) argv.push_back(argument.data());
    argv.push_back(nullptr);
    const auto deadline = Clock::now() + std::chrono::seconds(request.timeoutSeconds);
    const pid_t pid = fork();
    if (pid == 0) {
        if (!request.workingDirectory.empty() && chdir(request.workingDirectory.c_str()) != 0) _exit(126);
        if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) _exit(126);
        close(fd);
        execv(request.executable.c_str(), argv.data());
        _exit(127);
    }
    close(fd);
    if (pid < 0) {
        result.message = "process launch failed";
        return finish();
    }
    result.started = true;
    int status = 0;
    for (;;) {
        const auto state = waitpid(pid, &status, WNOHANG);
        if (state == pid) {
            result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            result.finished = true;
            return finish();
        }
        if (state < 0 && errno != EINTR) {
            result.message = "process wait failed";
            return finish();
        }
        if (Clock::now() >= deadline) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            result.timedOut = true;
            result.message = "process timeout";
            return finish();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
#endif
}

}  // namespace satrar::sat
