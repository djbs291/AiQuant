#pragma once
#include <optional>
#include <string>
#include <string_view>

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

    // The token scenarios, model files and the CLI spell a timeframe with. Here rather than in
    // fin_app so fin_stream can read a model file's "# timeframe:" without depending upward.
    inline const char *timeframe_token(Timeframe timeframe)
    {
        switch (timeframe)
        {
        case Timeframe::S1:
            return "S1";
        case Timeframe::S5:
            return "S5";
        case Timeframe::M5:
            return "M5";
        case Timeframe::H1:
            return "H1";
        case Timeframe::M1:
        default:
            return "M1";
        }
    }

    inline std::optional<Timeframe> timeframe_from_token(std::string_view token)
    {
        for (const Timeframe tf : {Timeframe::S1, Timeframe::S5, Timeframe::M1, Timeframe::M5, Timeframe::H1})
        {
            if (token == timeframe_token(tf))
                return tf;
        }
        return std::nullopt;
    }

} // namespace fin::io
