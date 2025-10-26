#include "catch2_compat.hpp"

#include "fin/app/ScenarioConfigIO.hpp"
#include "fin/app/ScenarioRunner.hpp"
#include "app/TestScenarioHelpers.hpp"

TEST_CASE("load_scenario_file parses canonical config", "[scenario][config]")
{
    auto path = scenario_test::write_temp_config(R"(
        # Comment line
        ticks = sample.csv
        tf = M5
        train_ratio = 0.8
        ema_fast = 8
        ema_slow = 20
        rsi = 10
        use_ema_crossover = off
        no_ema_xover = on
        preview = 7
    )");

    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE(fin::app::load_scenario_file(path.string(), cfg, error));
    REQUIRE(cfg.ticks_path == "sample.csv");
    REQUIRE(cfg.timeframe == fin::io::Timeframe::M5);
    REQUIRE(cfg.train_ratio == Approx(0.8));
    REQUIRE(cfg.ema_fast == 8);
    REQUIRE(cfg.ema_slow == 20);
    REQUIRE(cfg.rsi_period == 10);
    REQUIRE_FALSE(cfg.use_ema_crossover);
    REQUIRE(cfg.validation_preview_limit == 7);

    std::filesystem::remove(path);
}

TEST_CASE("load_scenario_file detects invalid booleans", "[scenario][config]")
{
    auto path = scenario_test::write_temp_config("ticks=data.csv\nuse_ema_crossover = maybe\n");
    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE_FALSE(fin::app::load_scenario_file(path.string(), cfg, error));
    REQUIRE(error.find("Invalid boolean") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("load_scenario_file requires ticks path", "[scenario][config]")
{
    auto path = scenario_test::write_temp_config("tf = M1\n");
    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE_FALSE(fin::app::load_scenario_file(path.string(), cfg, error));
    REQUIRE(error.find("ticks") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("run_scenario executes on synthetic CSV", "[scenario][runner]")
{
    const auto ticks = scenario_test::write_temp_ticks_csv(200);

    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = ticks.string();
    cfg.validation_preview_limit = 2;

    auto result = fin::app::run_scenario(cfg);
    REQUIRE(result.candles > 0);
    REQUIRE(result.feature_rows >= 3);
    REQUIRE(result.training.samples > 0);
    REQUIRE(result.metrics.trades >= 0);
    REQUIRE(result.validation_preview.size() <= cfg.validation_preview_limit);

    std::filesystem::remove(ticks);
}
