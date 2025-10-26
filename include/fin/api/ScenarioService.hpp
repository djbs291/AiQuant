#pragma once

#include <string>

#include "fin/app/ScenarioRunner.hpp"

namespace fin::api
{
    class ScenarioService
    {
    public:
        ScenarioService() = default;

        fin::app::ScenarioResult run(const fin::app::ScenarioConfig &cfg) const;
        fin::app::ScenarioResult run_file(const std::string &path) const;
        fin::app::ScenarioConfig load_file(const std::string &path) const;
    };
}

