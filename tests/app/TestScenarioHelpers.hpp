#pragma once

#include "TestTempFiles.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace scenario_test
{
    inline std::filesystem::path temp_path(const std::string &prefix, const std::string &suffix)
    {
        return test_files::temp_path(prefix, suffix);
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

    // Two instruments interleaved in the same minutes, long enough to clear the default
    // warmup. ABC sits around 100 and XYZ around 900, so any blending is unmissable.
    inline std::filesystem::path write_two_symbol_ticks(std::size_t minutes = 256)
    {
        auto path = temp_path("aiquant_two_symbols_", ".csv");
        std::ofstream out(path);
        out << "Timestamp,symbol,price,volume\n";
        const long long base_ts = 1693492800000LL;
        for (std::size_t i = 0; i < minutes; ++i)
        {
            const long long minute = base_ts + static_cast<long long>(i) * 60000;
            const double wobble = static_cast<double>(i % 10) * 0.5;
            out << minute << ",ABC," << (100.0 + wobble) << ",1\n";
            out << (minute + 1000) << ",XYZ," << (900.0 + wobble) << ",2\n";
        }
        return path;
    }
}
