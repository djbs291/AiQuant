#include "fin/app/ScenarioUtils.hpp"

namespace fin::app
{
    std::optional<fin::io::Timeframe> parse_timeframe_token(const std::string &token)
    {
        if (token == "S1")
            return fin::io::Timeframe::S1;
        if (token == "S5")
            return fin::io::Timeframe::S5;
        if (token == "M1")
            return fin::io::Timeframe::M1;
        if (token == "M5")
            return fin::io::Timeframe::M5;
        if (token == "H1")
            return fin::io::Timeframe::H1;
        return std::nullopt;
    }
}

