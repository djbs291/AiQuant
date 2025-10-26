#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string_view>

namespace scenario_test
{
    inline std::filesystem::path temp_path(const std::string &prefix, const std::string &suffix)
    {
        const auto ts = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::string filename = prefix + std::to_string(ts) + suffix;
        return std::filesystem::temp_directory_path() / filename;
    }

    inline std::filesystem::path write_temp_config(std::string_view contents)
    {
        auto path = temp_path("aiquant_scenario_", ".ini");
        std::ofstream out(path);
        out << contents;
        return path;
    }

    inline std::filesystem::path write_temp_ticks_csv(std::size_t rows = 200)
    {
        auto path = temp_path("aiquant_ticks_", ".csv");
        std::ofstream out(path);
        out << "Timestamp,symbol,price,volume\n";
        long long base_ts = 1693492800000LL;
        for (std::size_t i = 0; i < rows; ++i)
        {
            const long long row_ts = base_ts + static_cast<long long>(i) * 60000;
            const double price = 100.0 + static_cast<double>(i % 10) * 0.5;
            const double volume = 1.0 + static_cast<double>((i % 5) + 1);
            out << row_ts << ",TEST," << price << ',' << volume << '\n';
        }
        return path;
    }
}

