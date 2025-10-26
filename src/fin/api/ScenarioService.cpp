#include "fin/api/ScenarioService.hpp"

#include <stdexcept>

#include "fin/app/ScenarioConfigIO.hpp"

namespace fin::api
{
    fin::app::ScenarioResult ScenarioService::run(const fin::app::ScenarioConfig &cfg) const
    {
        return fin::app::run_scenario(cfg);
    }

    fin::app::ScenarioConfig ScenarioService::load_file(const std::string &path) const
    {
        fin::app::ScenarioConfig cfg{};
        std::string error;
        if (!fin::app::load_scenario_file(path, cfg, error))
            throw std::runtime_error(error);
        return cfg;
    }

    fin::app::ScenarioResult ScenarioService::run_file(const std::string &path) const
    {
        auto cfg = load_file(path);
        return run(cfg);
    }
}

