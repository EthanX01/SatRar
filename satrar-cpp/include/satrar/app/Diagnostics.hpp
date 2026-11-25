#pragma once

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

namespace satrar::app {

/// Command-line diagnostics (stderr). Prefix: [satrar][LEVEL] ...
class Diagnostics {
public:
    static void setVerbose(bool on) noexcept { verbose_ = on; }
    [[nodiscard]] static bool verbose() noexcept { return verbose_; }

    static void info(std::ostream& err, const std::string& msg) {
        if (verbose_) {
            err << "[satrar][INFO] " << msg << '\n';
        }
    }

    static void warn(std::ostream& err, const std::string& msg) {
        err << "[satrar][WARN] " << msg << '\n';
    }

    static void error(std::ostream& err, const std::string& msg) {
        err << "[satrar][ERROR] " << msg << '\n';
    }

    static void notice(std::ostream& out, const std::string& msg) {
        out << "[satrar][OK] " << msg << '\n';
    }

    template <typename Clock = std::chrono::steady_clock>
    static double elapsedSeconds(const typename Clock::time_point& start) {
        const auto end = Clock::now();
        return std::chrono::duration<double>(end - start).count();
    }

private:
    static inline bool verbose_ = false;
};

}  // namespace satrar::app