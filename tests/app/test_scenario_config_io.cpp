#include "catch2_compat.hpp"

#include <filesystem>
#include <string>

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

TEST_CASE("load_scenario_file rejects a key set twice, whatever the spelling", "[scenario][config]")
{
    // The second value used to win silently. Each pair below sets one field twice: the same
    // key, a different case, an alias, and the two opposite-meaning EMA toggles.
    const char *pairs[] = {
        "rsi = 10\nrsi = 20\n",
        "rsi = 10\nRSI = 20\n",
        "sma = 5\nsma_period = 7\n",
        "ticks_path = other.csv\n",
        "use_ema_crossover = on\nno_ema_xover = on\n",
    };

    for (const char *lines : pairs)
    {
        const auto path = scenario_test::write_temp_config(std::string("ticks = data.csv\n") + lines);
        fin::app::ScenarioConfig cfg{};
        std::string error;
        REQUIRE_FALSE(fin::app::load_scenario_file(path.string(), cfg, error));
        REQUIRE(error.find("Duplicate key") != std::string::npos);
        std::filesystem::remove(path);
    }

    // The message names both occurrences, so the user can pick which one to keep.
    const auto path = scenario_test::write_temp_config("ticks = data.csv\nsma = 5\n\nsma_period = 7\n");
    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE_FALSE(fin::app::load_scenario_file(path.string(), cfg, error));
    REQUIRE(error == "Duplicate key 'sma_period' at line 4: already set by 'sma' at line 2");
    std::filesystem::remove(path);
}

TEST_CASE("Every documented key and alias still loads", "[scenario][config]")
{
    // The dispatch runs on canonical names now, so an alias missing from canonical_key would
    // turn into an unknown key. This is the list in docs/ScenarioConfig.md, one file per key
    // so no two of them collide as duplicates.
    const char *lines[] = {
        "tf = M5", "timeframe = M5", "train_ratio = 0.8", "ridge = 0.1", "ridge_lambda = 0.1",
        "ema_fast = 8", "ema_slow = 20", "rsi = 10", "macd_fast = 8", "macd_slow = 20",
        "macd_signal = 5", "rsi_buy = 25", "rsi_sell = 75", "use_ema_crossover = off",
        "no_ema_xover = on", "cash = 500", "initial_cash = 500", "qty = 2", "trade_qty = 2",
        "fee = 0.5", "fee_per_trade = 0.5", "model_out = m.csv", "model_output = m.csv",
        "preview = 4", "preview_limit = 4", "features = close,rsi", "sma = 7", "sma_period = 7",
        "bb_period = 7", "bb_k = 1.5", "atr = 7", "atr_period = 7", "adx = 7", "adx_period = 7",
        "stoch_k = 7", "stoch_k_period = 7", "stoch_d = 2", "stoch_d_period = 2", "zscore = 7",
        "zscore_period = 7", "momentum = 7", "momentum_period = 7", "model = sgd",
        "sgd_learning_rate = 0.02", "sgd_lr = 0.02", "sgd_l2 = 0.001", "sgd_epochs = 3",
        "sgd_power_t = 0.5", "sgd_standardize = off", "symbol = ABC"};

    for (const char *line : lines)
    {
        const auto path = scenario_test::write_temp_config(std::string("ticks = data.csv\n") + line + "\n");
        fin::app::ScenarioConfig cfg{};
        std::string error;
        REQUIRE(fin::app::load_scenario_file(path.string(), cfg, error));
        std::filesystem::remove(path);
    }

    // The ticks aliases and the online aliases need a file of their own: the first because
    // it is the required key, the second because it needs model = sgd.
    for (const char *ticks_line : {"ticks = a.csv", "ticks_path = a.csv", "data = a.csv"})
    {
        const auto path = scenario_test::write_temp_config(std::string(ticks_line) + "\n");
        fin::app::ScenarioConfig cfg{};
        std::string error;
        REQUIRE(fin::app::load_scenario_file(path.string(), cfg, error));
        REQUIRE(cfg.ticks_path == "a.csv");
        std::filesystem::remove(path);
    }
    for (const char *online_line : {"online_update = on", "online = on"})
    {
        const auto path = scenario_test::write_temp_config(std::string("ticks = a.csv\nmodel = sgd\n") +
                                                           online_line + "\n");
        fin::app::ScenarioConfig cfg{};
        std::string error;
        REQUIRE(fin::app::load_scenario_file(path.string(), cfg, error));
        REQUIRE(cfg.online_update);
        std::filesystem::remove(path);
    }

    // And an alias still lands in the same field as its canonical key.
    const auto path = scenario_test::write_temp_config("ticks = a.csv\nsma_period = 9\nsgd_lr = 0.5\n");
    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE(fin::app::load_scenario_file(path.string(), cfg, error));
    REQUIRE(cfg.sma_period == 9);
    REQUIRE(cfg.sgd.learning_rate == Approx(0.5));
    std::filesystem::remove(path);
}

TEST_CASE("A '#' starts a comment only after whitespace", "[scenario][config]")
{
    // `ticks = runs#3.csv` used to load as `runs`, a different file, without a word.
    auto path = scenario_test::write_temp_config("ticks = runs#3.csv\nsymbol = A#B # the class\n");
    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE(fin::app::load_scenario_file(path.string(), cfg, error));
    REQUIRE(cfg.ticks_path == "runs#3.csv");
    REQUIRE(cfg.symbol == "A#B");
    std::filesystem::remove(path);

    // An inline comment after whitespace, a tab included, still works.
    path = scenario_test::write_temp_config("ticks = a.csv # the data\nrsi = 10\t# shorter\n");
    fin::app::ScenarioConfig commented{};
    error.clear();
    REQUIRE(fin::app::load_scenario_file(path.string(), commented, error));
    REQUIRE(commented.ticks_path == "a.csv");
    REQUIRE(commented.rsi_period == 10);
    std::filesystem::remove(path);

    // A number glued to a '#' used to be cut at it and read as 10; now it is not a number.
    path = scenario_test::write_temp_config("ticks = a.csv\nrsi = 10#x\n");
    fin::app::ScenarioConfig glued{};
    error.clear();
    REQUIRE_FALSE(fin::app::load_scenario_file(path.string(), glued, error));
    REQUIRE(error.find("line 2") != std::string::npos);
    std::filesystem::remove(path);
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
