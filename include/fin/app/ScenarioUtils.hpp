#pragma once

#include <optional>
#include <string>

#include "fin/io/Pipeline.hpp"

namespace fin::app
{
    std::optional<fin::io::Timeframe> parse_timeframe_token(const std::string &token);
}

