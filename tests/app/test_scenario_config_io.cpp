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

TEST_CASE("load_scenario_file rejects an unknown key", "[scenario][config]")
{
    // Silence used to be the policy: a typo left the period at its default and the run looked
    // entirely normal, training a different model than the one asked for.
    auto path = scenario_test::write_temp_config("ticks=data.csv\nrsi_peroid = 20\n");
    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE_FALSE(fin::app::load_scenario_file(path.string(), cfg, error));
    REQUIRE(error.find("rsi_peroid") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("load_scenario_file rejects a zero period", "[scenario][config]")
{
    // A zero period does not fail loudly on its own: the indicator never becomes ready and
    // the run dies much later with "Insufficient data after indicator warmup", blaming the
    // data for what is a configuration mistake.
    auto path = scenario_test::write_temp_config("ticks=data.csv\nema_fast = 0\n");
    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE_FALSE(fin::app::load_scenario_file(path.string(), cfg, error));
    REQUIRE(error.find("ema_fast") != std::string::npos);
    REQUIRE(error.find(">= 1") != std::string::npos);

    // A fresh config per load: load_scenario_file fills the struct as it parses, so a failed
    // load leaves its values behind, and the range check at the end reads whatever is there.
    // Reusing `cfg` here would report ema_fast again and say nothing about bb_period.
    auto bb = scenario_test::write_temp_config("ticks=data.csv\nbb_period = 0\n");
    fin::app::ScenarioConfig bb_cfg{};
    error.clear();
    REQUIRE_FALSE(fin::app::load_scenario_file(bb.string(), bb_cfg, error));
    REQUIRE(error.find("bb_period") != std::string::npos);

    std::filesystem::remove(path);
    std::filesystem::remove(bb);
}

TEST_CASE("load_scenario_file rejects nonsensical numeric ranges", "[scenario][config]")
{
    // A negative ridge term is not regularization but its opposite, and it passed silently.
    auto ridge = scenario_test::write_temp_config("ticks=data.csv\nridge = -1\n");
    fin::app::ScenarioConfig ridge_cfg{};
    std::string error;
    REQUIRE_FALSE(fin::app::load_scenario_file(ridge.string(), ridge_cfg, error));
    REQUIRE(error.find("ridge") != std::string::npos);

    // Again a fresh config, so this asserts bb_k on its own merits rather than on the order
    // the checks happen to run in.
    auto width = scenario_test::write_temp_config("ticks=data.csv\nbb_k = 0\n");
    fin::app::ScenarioConfig width_cfg{};
    error.clear();
    REQUIRE_FALSE(fin::app::load_scenario_file(width.string(), width_cfg, error));
    REQUIRE(error.find("bb_k") != std::string::npos);

    std::filesystem::remove(ridge);
    std::filesystem::remove(width);
}

TEST_CASE("The scenarios shipped in the repo still load", "[scenario][config]")
{
    // Rejecting unknown keys is a behaviour change, so the files the project ships are the
    // regression guard: if one of them stops loading, the new strictness went too far.
    const char *shipped[] = {
        AIQUANT_SOURCE_DIR "/scenarios/mvp.ini",
        AIQUANT_SOURCE_DIR "/examples/wide_features.ini",
        AIQUANT_SOURCE_DIR "/examples/sgd_online.ini",
    };

    for (const char *path : shipped)
    {
        fin::app::ScenarioConfig cfg{};
        std::string error;
        REQUIRE(fin::app::load_scenario_file(path, cfg, error));
        REQUIRE(error.empty());
    }
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
