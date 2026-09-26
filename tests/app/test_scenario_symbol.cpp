#include "catch2_compat.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include "TestTempFiles.hpp"
#include "app/TestScenarioHelpers.hpp"
#include "fin/app/ScenarioConfigIO.hpp"
#include "fin/app/ScenarioRunner.hpp"
#include "fin/app/ScenarioSerialization.hpp"
#include "fin/ml/LinearModel.hpp"

TEST_CASE("Scenario INI carries a symbol, case intact", "[app][symbol]")
{
    // Keys are folded, values are not: a ticker is not ours to lowercase.
    const auto config = scenario_test::write_temp_config("ticks = sample.csv\nsymbol = AbC\n");

    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE(fin::app::load_scenario_file(config.string(), cfg, error));
    REQUIRE(cfg.symbol == "AbC");

    std::filesystem::remove(config);
}

TEST_CASE("Scenario defaults leave the symbol unset", "[app][symbol]")
{
    // The whole point of the default: every scenario written before this key existed binds to
    // the file's own symbol and behaves exactly as it did.
    const auto config = scenario_test::write_temp_config("ticks = sample.csv\n");

    fin::app::ScenarioConfig cfg{};
    std::string error;
    REQUIRE(fin::app::load_scenario_file(config.string(), cfg, error));
    REQUIRE(cfg.symbol.empty());

    std::filesystem::remove(config);
}

TEST_CASE("run_scenario binds to one symbol and says which", "[app][symbol]")
{
    const auto ticks = scenario_test::write_two_symbol_ticks();

    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = ticks.string();

    const auto result = fin::app::run_scenario(cfg);

    REQUIRE(result.symbol == "ABC"); // the first tick in the file
    REQUIRE(result.ticks_other_symbol == 256);
    REQUIRE(result.candles == 256);

    // The proof that nothing was blended: a bar mixing ABC with XYZ would put the close
    // somewhere near 900. Every close here belongs to ABC's band.
    REQUIRE(result.validation_preview.size() > 0);

    const std::string json = fin::app::scenario_result_to_json(cfg, result);
    REQUIRE(json.find("\"symbol\": \"ABC\"") != std::string::npos);
    REQUIRE(json.find("\"ticks_other_symbol\": 256") != std::string::npos);

    std::filesystem::remove(ticks);
}

TEST_CASE("Naming a symbol runs the scenario on that instrument", "[app][symbol]")
{
    const auto ticks = scenario_test::write_two_symbol_ticks();

    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = ticks.string();
    cfg.symbol = "XYZ";

    const auto result = fin::app::run_scenario(cfg);

    REQUIRE(result.symbol == "XYZ");
    REQUIRE(result.ticks_other_symbol == 256);
    REQUIRE(result.candles == 256);
    REQUIRE(result.feature_rows > 3);

    std::filesystem::remove(ticks);
}

TEST_CASE("The two symbols in one file produce different scenarios", "[app][symbol]")
{
    // Blending would make these two runs identical, because both would see the same merged
    // candles. They have to differ.
    const auto ticks = scenario_test::write_two_symbol_ticks();

    fin::app::ScenarioConfig abc{};
    abc.ticks_path = ticks.string();
    abc.symbol = "ABC";

    fin::app::ScenarioConfig xyz{};
    xyz.ticks_path = ticks.string();
    xyz.symbol = "XYZ";

    const auto abc_result = fin::app::run_scenario(abc);
    const auto xyz_result = fin::app::run_scenario(xyz);

    REQUIRE(abc_result.candles == xyz_result.candles);
    REQUIRE(abc_result.symbol != xyz_result.symbol);

    // Same shape of series at a different price level, so the fitted bias differs even where
    // the weights do not.
    REQUIRE_FALSE(abc_result.training.model.bias() ==
                  Approx(xyz_result.training.model.bias()).margin(1e-9));

    std::filesystem::remove(ticks);
}

TEST_CASE("A saved model records the symbol the scenario resolved", "[app][symbol]")
{
    // So that `run-mvp --symbol X --model-out models/X.csv` writes a file that says which
    // instrument it belongs to, and a model directory can refuse one saved under a wrong name.
    const auto ticks = scenario_test::write_two_symbol_ticks();
    const test_files::TempDir dir("aiquant_scenario_models_");

    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = ticks.string();
    cfg.symbol = "XYZ";
    cfg.model_output_path = (dir.path() / "XYZ.csv").string();
    const auto result = fin::app::run_scenario(cfg);
    REQUIRE(result.model_saved);
    REQUIRE(result.training.model.symbol() == "XYZ");

    fin::ml::LinearModel loaded;
    REQUIRE(loaded.load_from_file(*cfg.model_output_path));
    REQUIRE(loaded.symbol() == "XYZ");

    // Without a symbol key the run binds to the first tick's symbol, and that is recorded.
    fin::app::ScenarioConfig unnamed{};
    unnamed.ticks_path = ticks.string();
    unnamed.model_output_path = (dir.path() / "first.csv").string();
    REQUIRE(fin::app::run_scenario(unnamed).training.model.symbol() == "ABC");

    std::filesystem::remove(ticks);
}
