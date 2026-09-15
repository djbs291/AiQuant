#pragma once

// Helpers for tests that need a real file on disk. Fixtures are created in the
// system temp directory (never in the current working directory), so running the
// tests from the repo root does not write or overwrite files in the repo.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace test_files
{
    inline long long process_id()
    {
#if defined(_WIN32)
        return static_cast<long long>(_getpid());
#else
        return static_cast<long long>(getpid());
#endif
    }

    // Unique across processes (pid) and across calls within one process (clock).
    inline std::filesystem::path temp_path(std::string_view prefix, std::string_view suffix)
    {
        const auto ts = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::string filename = std::string(prefix) + std::to_string(process_id()) + "_" +
                               std::to_string(ts) + std::string(suffix);
        return std::filesystem::temp_directory_path() / filename;
    }

    // Writes contents to a unique temp file and removes it on destruction.
    class TempFile
    {
    public:
        TempFile(std::string_view prefix, std::string_view suffix, std::string_view contents)
            : path_(temp_path(prefix, suffix))
        {
            std::ofstream out(path_);
            out << contents;
        }

        ~TempFile()
        {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
        }

        TempFile(const TempFile &) = delete;
        TempFile &operator=(const TempFile &) = delete;

        const std::filesystem::path &path() const { return path_; }
        std::string string() const { return path_.string(); }

    private:
        std::filesystem::path path_;
    };
}
