#pragma once

#include <string>

#include "fin/app/ScenarioRunner.hpp"

namespace fin::app
{
    // Parse an INI-like scenario file. Returns true on success and fills `cfg`;
    // otherwise returns false and writes a human-readable error to `error`.
    bool load_scenario_file(const std::string &path, ScenarioConfig &cfg, std::string &error);
}

