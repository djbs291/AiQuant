#pragma once

#include <string>

#include "fin/app/ScenarioRunner.hpp"

namespace fin::app
{
    std::string scenario_result_to_json(const ScenarioConfig &cfg, const ScenarioResult &result);
    bool write_validation_preview_csv(const ScenarioResult &result, const std::string &path);
}

