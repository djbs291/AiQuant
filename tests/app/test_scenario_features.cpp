#include "catch2_compat.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "app/TestScenarioHelpers.hpp"
#include "TestTempFiles.hpp"
#include "fin/app/ScenarioConfigIO.hpp"
#include "fin/app/ScenarioRunner.hpp"
#include "fin/ml/FeatureVector.hpp"
#include "fin/ml/LinearModel.hpp"

TEST_CASE("Scenario INI parses a feature list, trimming and lowercasing", "[app][features]")
{
    using namespace scenario_test;

    const auto ticks = write_temp_ticks_csv(256);
    const auto config = write_temp_config(std::string("ticks = ") + ticks.string() +
                                          "\nfeatures = close, RSI , atr\n");

    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE(fin::app::load_scenario_file(config.string(), cfg, error));

    const std::vector<std::string> expected = {"close", "rsi", "atr"};
    REQUIRE(cfg.features == expected);

    std::filesystem::remove(ticks);
    std::filesystem::remove(config);
}

TEST_CASE("Scenario INI rejects unknown and duplicate feature names", "[app][features]")
{
    using namespace scenario_test;

    const auto ticks = write_temp_ticks_csv(32);

    const auto unknown = write_temp_config(std::string("ticks = ") + ticks.string() +
                                           "\nfeatures = close,bogus\n");
    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE_FALSE(fin::app::load_scenario_file(unknown.string(), cfg, error));
    REQUIRE(error.find("bogus") != std::string::npos);

    const auto duplicate = write_temp_config(std::string("ticks = ") + ticks.string() +
                                             "\nfeatures = close,rsi,close\n");
    error.clear();
    REQUIRE_FALSE(fin::app::load_scenario_file(duplicate.string(), cfg, error));
    REQUIRE(error.find("duplicate") != std::string::npos);

    std::filesystem::remove(ticks);
    std::filesystem::remove(unknown);
    std::filesystem::remove(duplicate);
}

TEST_CASE("run_scenario reports the resolved feature set", "[app][features]")
{
    using namespace scenario_test;

    const auto ticks = write_temp_ticks_csv(256);

    // Omitted key: the historical six.
    fin::app::ScenarioConfig defaults{};
    defaults.ticks_path = ticks.string();
    const auto default_result = fin::app::run_scenario(defaults);
    REQUIRE(default_result.features == fin::indicators::default_feature_names());

    // Explicit set: reported back in the requested order.
    fin::app::ScenarioConfig wide{};
    wide.ticks_path = ticks.string();
    wide.features = {"close", "ema_fast", "rsi", "atr"};
    const auto wide_result = fin::app::run_scenario(wide);
    REQUIRE(wide_result.features == wide.features);
    REQUIRE(wide_result.feature_rows > 3);
    REQUIRE(wide_result.training.model.named_weights().size() == wide.features.size());

    std::filesystem::remove(ticks);
}

TEST_CASE("run_scenario names the feature set when warmup starves it", "[app][features]")
{
    using namespace scenario_test;

    // 12 candles cannot clear an ADX warmup of 2N-1 = 27.
    const auto ticks = write_temp_ticks_csv(12);
    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = ticks.string();
    cfg.features = {"close", "adx"};

    std::string message;
    try
    {
        fin::app::run_scenario(cfg);
    }
    catch (const std::runtime_error &ex)
    {
        message = ex.what();
    }

    REQUIRE(message.find("Insufficient data") != std::string::npos);
    REQUIRE(message.find("adx") != std::string::npos);

    std::filesystem::remove(ticks);
}

TEST_CASE("A model file records its feature set and validate_schema checks it", "[ml][features]")
{
    const test_files::TempFile model("aiquant_model_schema_", ".csv",
                                     "# AiQuant LinearModel weights\n"
                                     "# features: close,rsi\n"
                                     "bias,0.5\n"
                                     "close,0.1\n"
                                     "rsi,0.2\n");

    fin::ml::LinearModel loaded;
    REQUIRE(loaded.load_from_file(model.string()));

    const std::vector<std::string> recorded = {"close", "rsi"};
    REQUIRE(loaded.feature_names() == recorded);

    fin::ml::FeatureVector matching;
    matching.names = recorded;
    matching.values = {100.0, 55.0};
    loaded.validate_schema(matching); // must not throw

    fin::ml::FeatureVector mismatched;
    mismatched.names = {"close", "atr"};
    mismatched.values = {100.0, 1.5};

    bool threw = false;
    try
    {
        loaded.validate_schema(mismatched);
    }
    catch (const std::invalid_argument &)
    {
        threw = true;
    }
    REQUIRE(threw);
}

TEST_CASE("A model file without a feature line still loads and validates", "[ml][features]")
{
    // Files written before the schema line existed must keep working.
    const test_files::TempFile legacy("aiquant_model_legacy_", ".csv",
                                      "# AiQuant LinearModel weights\n"
                                      "bias,0.2\n"
                                      "close,0.1\n");

    fin::ml::LinearModel loaded;
    REQUIRE(loaded.load_from_file(legacy.string()));
    REQUIRE(loaded.feature_names().empty());

    fin::ml::FeatureVector anything;
    anything.names = {"close", "atr"};
    anything.values = {100.0, 1.5};
    loaded.validate_schema(anything); // no recorded set, so nothing to contradict
}
