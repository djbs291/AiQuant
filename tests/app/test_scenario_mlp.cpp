#include "catch2_compat.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>

#include "app/TestScenarioHelpers.hpp"
#include "fin/app/ScenarioConfigIO.hpp"
#include "fin/app/ScenarioRunner.hpp"
#include "fin/app/ScenarioSerialization.hpp"
#include "fin/ml/MlpRegressor.hpp"

TEST_CASE("Scenario INI selects the MLP trainer and its knobs", "[app][mlp]")
{
    const auto config = scenario_test::write_temp_config(
        "ticks = sample.csv\n"
        "model = MLP\n" // the value is case-folded like the keys are
        "mlp_hidden = 16,8\n"
        "mlp_lr = 0.02\n"
        "mlp_l2 = 1e-7\n"
        "mlp_epochs = 150\n"
        "mlp_activation = relu\n"
        "mlp_seed = 7\n");

    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE(fin::app::load_scenario_file(config.string(), cfg, error));
    REQUIRE(cfg.model == fin::app::ModelKind::Mlp);
    REQUIRE(cfg.mlp.hidden_layers.size() == 2);
    REQUIRE(cfg.mlp.hidden_layers[0] == 16);
    REQUIRE(cfg.mlp.hidden_layers[1] == 8);
    REQUIRE(cfg.mlp.learning_rate == Approx(0.02).margin(1e-12));
    REQUIRE(cfg.mlp.l2 == Approx(1e-7).margin(1e-15));
    REQUIRE(cfg.mlp.epochs == 150);
    REQUIRE(cfg.mlp.activation == fin::ml::MlpActivation::Relu);
    REQUIRE(cfg.mlp.seed == 7);

    std::filesystem::remove(config);
}

TEST_CASE("Scenario INI rejects a zero-width hidden layer", "[app][mlp]")
{
    const auto config = scenario_test::write_temp_config(
        "ticks = sample.csv\nmodel = mlp\nmlp_hidden = 8,0\n");

    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE_FALSE(fin::app::load_scenario_file(config.string(), cfg, error));
    REQUIRE(error.find("mlp_hidden") != std::string::npos);

    std::filesystem::remove(config);
}

TEST_CASE("run_scenario trains an MLP and reports it", "[app][mlp]")
{
    const auto ticks = scenario_test::write_temp_ticks_csv(256);

    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = ticks.string();
    cfg.model = fin::app::ModelKind::Mlp;
    cfg.mlp.epochs = 50; // keep the test quick

    const auto result = fin::app::run_scenario(cfg);
    REQUIRE(result.model == "mlp");
    REQUIRE(result.training.samples > 0);
    // The MLP has no linear export, so no named weights are reported for it.
    REQUIRE(result.training.model.named_weights().empty());
    // It still gets a validation score and a backtest, out of sample and in sample.
    REQUIRE(result.validation_samples > 0);
    REQUIRE(result.out_of_sample_candles > 0);

    const std::string json = fin::app::scenario_result_to_json(cfg, result);
    REQUIRE(json.find("\"model\": \"mlp\"") != std::string::npos);

    std::filesystem::remove(ticks);
}

TEST_CASE("run_scenario refuses model_out for an MLP", "[app][mlp]")
{
    const auto ticks = scenario_test::write_temp_ticks_csv(64);

    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = ticks.string();
    cfg.model = fin::app::ModelKind::Mlp;
    cfg.model_output_path = "should_not_be_written.csv";

    std::string message;
    try
    {
        fin::app::run_scenario(cfg);
    }
    catch (const std::invalid_argument &ex)
    {
        message = ex.what();
    }
    REQUIRE(message.find("model_out is not supported for model = mlp") != std::string::npos);
    // Refused before anything was written.
    REQUIRE_FALSE(std::filesystem::exists("should_not_be_written.csv"));

    std::filesystem::remove(ticks);
}

TEST_CASE("Online updating with the MLP is refused", "[app][mlp]")
{
    const auto ticks = scenario_test::write_temp_ticks_csv(64);

    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = ticks.string();
    cfg.model = fin::app::ModelKind::Mlp;
    cfg.online_update = true; // online learning is wired for sgd only

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
