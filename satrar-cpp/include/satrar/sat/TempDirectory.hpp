
#pragma once

#include <chrono>
#include <filesystem>
#include <random>

namespace satrar::sat {

class TempDirectory {
public:
    TempDirectory() {
        const auto base = std::filesystem::temp_directory_path();
        std::mt19937_64 random(std::random_device{}() ^
                               std::chrono::steady_clock::now().time_since_epoch().count());
        do {
            path_ = base / ("satrar-" + std::to_string(random()));
        } while (!std::filesystem::create_directory(path_));
    }
    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace satrar::sat
