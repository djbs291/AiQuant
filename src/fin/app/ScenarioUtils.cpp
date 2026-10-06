#include "fin/app/ScenarioUtils.hpp"
#include <optional>

namespace fin::app
{
    std::optional<fin::io::Timeframe> parse_timeframe_token(const std::string &token)
    {
        return fin::io::timeframe_from_token(token);
    }
}
