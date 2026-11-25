
#include "satrar/sat/CadicalBackend.hpp"
#include "satrar/sat/TempDirectory.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
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
#endif

namespace satrar::sat {
namespace {

using Clock = std::chrono::steady_clock;

std::filesystem::path defaultCadicalExecutable() {
    if (const char* configured = std::getenv("SATRAR_CADICAL_EXE"); configured && *configured) {
        return configured;
    }
#ifdef SATRAR_CONFIGURED_CADICAL
    return SATRAR_CONFIGURED_CADICAL;
#endif
#ifdef SATRAR_BUNDLED_CADICAL
#ifdef _WIN32
    std::wstring module(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (size && size < module.size()) {
        module.resize(size);
        const auto sibling = std::filesystem::path(module).parent_path() / "satrar_cadical.exe";
        if (std::filesystem::is_regular_file(sibling)) {
            return sibling;
        }
    }
#else
    std::error_code ignored;
    const auto proc = std::filesystem::read_symlink("/proc/self/exe", ignored);
    const auto sibling = proc.parent_path() / "satrar_cadical";
    if (!ignored && std::filesystem::is_regular_file(sibling)) {
        return sibling;
    }
#endif
    return SATRAR_BUNDLED_CADICAL;
#else
    return {};
#endif
}

bool runSolver(const std::filesystem::path& executable, const std::filesystem::path& input,
               const std::filesystem::path& log, std::uint32_t timeout, SatSolveResult& result) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    const auto output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
                                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        result.message = "cannot create solver output: " + std::to_string(GetLastError());
        return false;
    }
    const auto stdinHandle = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOEXW info{};
    info.StartupInfo.cb = sizeof(info);
    info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    info.StartupInfo.hStdInput = stdinHandle;
    info.StartupInfo.hStdOutput = info.StartupInfo.hStdError = output;
    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    std::vector<char> attributes(attributeSize);
    info.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    const bool initialized = attributeSize &&
                             InitializeProcThreadAttributeList(info.lpAttributeList, 1, 0, &attributeSize);
    HANDLE inherited[] = {stdinHandle, output};
    const bool restricted = initialized && stdinHandle != INVALID_HANDLE_VALUE &&
        UpdateProcThreadAttribute(info.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                  inherited, sizeof(inherited), nullptr, nullptr);
    PROCESS_INFORMATION process{};
    auto command = L"\"" + executable.wstring() + L"\" \"" + input.wstring() + L"\"";
    const auto started = restricted &&
        CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                       &info.StartupInfo, &process);
    const auto startError = GetLastError();
    if (initialized) {
        DeleteProcThreadAttributeList(info.lpAttributeList);
    }
    CloseHandle(output);
    if (stdinHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(stdinHandle);
    }
    if (!started) {
        result.message = "CaDiCaL launch failed: Windows error " + std::to_string(startError);
        return false;
    }
    const auto wait = WaitForSingleObject(process.hProcess,
        static_cast<DWORD>(std::min<std::uint64_t>(timeout * 1000ull, INFINITE - 1ull)));
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
        result.message = wait == WAIT_TIMEOUT ? "solver timeout" : "solver wait failed";
    } else {
        DWORD status = 0;
        if (GetExitCodeProcess(process.hProcess, &status)) {
            result.exitCode = static_cast<int>(status);
        }
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return wait == WAIT_OBJECT_0;
#else
    const int fd = open(log.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0) {
        result.message = "cannot create solver output";
        return false;
    }
    const auto bin = executable.c_str();
    const auto cnf = input.c_str();
    const auto deadline = Clock::now() + std::chrono::seconds(timeout);
    const pid_t pid = fork();
    if (pid == 0) {
        if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
            _exit(126);
        }
        close(fd);
        execl(bin, bin, cnf, static_cast<char*>(nullptr));
        _exit(127);
    }
    close(fd);
    if (pid < 0) {
        result.message = "CaDiCaL launch failed";
        return false;
    }
    int status = 0;
    for (;;) {
        const auto state = waitpid(pid, &status, WNOHANG);
        if (state == pid) {
            result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            return true;
        }
        if (state < 0 && errno != EINTR) {
            result.message = "solver wait failed";
            return false;
        }
        if (Clock::now() >= deadline) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            result.message = "solver timeout";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
#endif
}

}  // namespace

CadicalBackend::CadicalBackend() : executable_(defaultCadicalExecutable()) {}
CadicalBackend::CadicalBackend(std::filesystem::path executable) : executable_(std::move(executable)) {}

void CadicalBackend::setExecutable(std::filesystem::path executable) {
    executable_ = std::move(executable);
}

bool CadicalBackend::available() const {
    std::error_code error;
    return !executable_.empty() && std::filesystem::is_regular_file(executable_, error);
}

std::string CadicalBackend::describe() const { return "CaDiCaL: " + executable_.string(); }

SatSolveResult CadicalBackend::solveDimacsFile(const std::filesystem::path& input,
                                               std::optional<std::uint32_t> timeout) const {
    SatSolveResult result;
    const auto start = Clock::now();
    if (!available() || !std::filesystem::is_regular_file(input)) {
        result.message = "missing CaDiCaL executable or DIMACS file";
        return result;
    }
    const TempDirectory temporary;
    const auto log = temporary.path() / "solver.log";
    const auto finished = runSolver(std::filesystem::absolute(executable_), std::filesystem::absolute(input),
                                    log, timeout.value_or(300), result);
    result.elapsedSeconds = std::chrono::duration<double>(Clock::now() - start).count();
    if (!finished) {
        if (result.message == "solver timeout") {
            result.verdict = SatVerdict::Timeout;
        }
        return result;
    }
    switch (result.exitCode) {
        case 10: result.verdict = SatVerdict::Sat; break;
        case 20: result.verdict = SatVerdict::Unsat; break;
        case 0: result.verdict = SatVerdict::Unknown; break;
        default: result.verdict = SatVerdict::Error; break;
    }
    result.message = "CaDiCaL exit " + std::to_string(result.exitCode);
    if (result.verdict == SatVerdict::Sat) {
        std::ifstream output(log);
        std::string line;
        while (std::getline(output, line)) {
            if (line.rfind("v ", 0) != 0) {
                continue;
            }
            std::istringstream model(line.substr(2));
            std::int32_t lit = 0;
            while (model >> lit && lit != 0) {
                result.modelLits.push_back(lit);
            }
        }
    }
    return result;
}

std::unique_ptr<SatBackend> defaultVerificationBackend() {
    return std::make_unique<CadicalBackend>();
}

}  // namespace satrar::sat
