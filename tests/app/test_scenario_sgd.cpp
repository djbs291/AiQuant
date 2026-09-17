#include "catch2_compat.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>

#include "app/TestScenarioHelpers.hpp"
#include "fin/app/ScenarioConfigIO.hpp"
#include "fin/app/ScenarioRunner.hpp"
#include "fin/app/ScenarioSerialization.hpp"

TEST_CASE("Scenario INI selects the SGD trainer and its knobs", "[app][sgd]")
{
    const auto config = scenario_test::write_temp_config(
        "ticks = sample.csv\n"
        "model = SGD\n" // the value is case-folded like the keys are
        "sgd_lr = 0.05\n"
        "sgd_l2 = 1e-7\n"
        "sgd_epochs = 25\n"
        "sgd_power_t = 0.5\n"
        "sgd_standardize = off\n"
        "online_update = yes\n");

    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE(fin::app::load_scenario_file(config.string(), cfg, error));
    REQUIRE(cfg.model == fin::app::ModelKind::Sgd);
    REQUIRE(cfg.sgd.learning_rate == Approx(0.05).margin(1e-12));
    REQUIRE(cfg.sgd.l2 == Approx(1e-7).margin(1e-15));
    REQUIRE(cfg.sgd.epochs == 25);
    REQUIRE(cfg.sgd.power_t == Approx(0.5).margin(1e-12));
    REQUIRE_FALSE(cfg.sgd.standardize);
    REQUIRE(cfg.online_update);

    std::filesystem::remove(config);
}

TEST_CASE("Scenario INI rejects an unknown model", "[app][sgd]")
{
    const auto config = scenario_test::write_temp_config("ticks = sample.csv\nmodel = forest\n");

    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE_FALSE(fin::app::load_scenario_file(config.string(), cfg, error));
    REQUIRE(error.find("forest") != std::string::npos);

    std::filesystem::remove(config);
}

TEST_CASE("Scenario defaults keep the ridge trainer", "[app][sgd]")
{
    // The whole point of the default: a scenario written before this existed trains the same
    // model it always did.
    const auto config = scenario_test::write_temp_config("ticks = sample.csv\n");

    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE(fin::app::load_scenario_file(config.string(), cfg, error));
    REQUIRE(cfg.model == fin::app::ModelKind::Ridge);
    REQUIRE_FALSE(cfg.online_update);

    std::filesystem::remove(config);
}

TEST_CASE("run_scenario trains an SGD model and exports it like a ridge one", "[app][sgd]")
{
    const auto ticks = scenario_test::write_temp_ticks_csv(256);

    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = ticks.string();
    cfg.model = fin::app::ModelKind::Sgd;

    const auto result = fin::app::run_scenario(cfg);
    REQUIRE(result.model == "sgd");
    REQUIRE(result.training.samples > 0);
    // One named weight per feature, so the report, the model file and /predict all work on
    // an SGD run exactly as they do on a ridge one.
    REQUIRE(result.training.model.named_weights().size() == result.features.size());

    // Nothing online was asked for, so nothing online is claimed.
    REQUIRE_FALSE(result.online_update);
    REQUIRE(result.online_updates == 0);
    REQUIRE(result.online_validation_rmse == Approx(0.0).margin(1e-12));

    std::filesystem::remove(ticks);
}

TEST_CASE("Online updating learns from exactly the out-of-sample candles", "[app][sgd]")
{
    const auto ticks = scenario_test::write_temp_ticks_csv(256);

    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = ticks.string();
    cfg.model = fin::app::ModelKind::Sgd;
    cfg.online_update = true;

    const auto result = fin::app::run_scenario(cfg);
    REQUIRE(result.online_update);
    REQUIRE(result.online_updates > 0);
    // The replay updates on every candle past the training split and on no other: that is
    // the same set of rows the validation loop scores, so the two counts have to agree. If
    // they ever diverge, the backtest has either skipped a bar or peeked at a training one.
    REQUIRE(result.online_updates == result.validation_samples);

    const std::string json = fin::app::scenario_result_to_json(cfg, result);
    REQUIRE(json.find("\"model\": \"sgd\"") != std::string::npos);
    REQUIRE(json.find("\"online_update\": true") != std::string::npos);
    REQUIRE(json.find("\"online_updates\": ") != std::string::npos);
    REQUIRE(json.find("\"online_validation_rmse\": ") != std::string::npos);

    std::filesystem::remove(ticks);
}

TEST_CASE("Online updating without the SGD model is refused", "[app][sgd]")
{
    const auto ticks = scenario_test::write_temp_ticks_csv(64);

    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = ticks.string();
    cfg.online_update = true; // model left at Ridge, which has no partial_fit

    std::string message;
    try
    {
        fin::app::run_scenario(cfg);
    }
    catch (const std::invalid_argument &ex)
    {
        message = ex.what();
    }
    REQUIRE(message.find("online_update requires model = sgd") != std::string::npos);

    std::filesystem::remove(ticks);
}
