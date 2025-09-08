#pragma once
#include <string>

namespace fin::io
{
    enum class TimeFormat
    {
        EpochMillis,
        ISO8601
    };

    struct TickCsvOptions
    {
        char delimiter = ',';
        bool has_header = true;
        TimeFormat ts_format = TimeFormat::EpochMillis; // MVP default
        // Column names (MVP assumes these exact names; relax later if needed)
        std::string ts_col = "Timestamp";
        std::string symbol_col = "symbol";
        std::string price_col = "price";
        std::string volume_col = "volume";
    };

    enum class Timeframe
    {
        S1,
        S5,
        M1,
        M5,
        H1
    }; // add as needed M5, H1,

} // namespace fin::io
