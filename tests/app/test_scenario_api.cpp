#include "catch2_compat.hpp"

#include <filesystem>

#include "fin/api/ScenarioService.hpp"
#include "app/TestScenarioHelpers.hpp"

TEST_CASE("ScenarioService loads scenario file", "[api][scenario]")
{
    using namespace scenario_test;

    const auto ticks = write_temp_ticks_csv(256);
    const auto config = write_temp_config(std::string("ticks = ") + ticks.string() + "\npreview = 2\n");

    fin::api::ScenarioService svc;

    auto cfg = svc.load_file(config.string());
    REQUIRE(cfg.ticks_path == ticks.string());
    REQUIRE(cfg.validation_preview_limit == 2);

    auto result = svc.run(cfg);
    REQUIRE(result.candles > 0);
    REQUIRE(result.feature_rows >= 3);

    auto result_from_file = svc.run_file(config.string());
    REQUIRE(result_from_file.candles == result.candles);

    std::filesystem::remove(ticks);
    std::filesystem::remove(config);
}
