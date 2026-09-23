#include "catch2_compat.hpp"

#include <filesystem>
#include <sstream>
#include <string>

#include "app/TestScenarioHelpers.hpp"
#include "fin/app/Json.hpp"
#include "fin/app/ScenarioRunner.hpp"
#include "fin/app/ScenarioSerialization.hpp"

// The writer and the reader in this project are two halves of one contract, and until now
// they disagreed: fin::app::json::parse refuses a bare `nan`, while the scenario serializer
// could emit one. These tests hold the two sides together by feeding the writer's output
// straight back into the reader.

TEST_CASE("Scenario JSON is always parseable by the project's own reader", "[app][json]")
{
    const auto ticks = scenario_test::write_temp_ticks_csv(256);

    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = ticks.string();

    const auto result = fin::app::run_scenario(cfg);
    const std::string document = fin::app::scenario_result_to_json(cfg, result);

    std::string error;
    const auto parsed = fin::app::json::parse(document, error);
    REQUIRE(parsed.has_value());
    REQUIRE(error.empty());

    std::filesystem::remove(ticks);
}

TEST_CASE("A non-finite result yields null, never a bare nan", "[app][json]")
{
    // This used to be reached with `bb_period = 0`, which run_scenario accepted from a config
    // built in code and which left the fit degenerate. run_scenario now refuses that config,
    // but a NaN can still come out of a valid one (a model can diverge, a series can be
    // flat), so the writer's half of the contract is held here by poisoning a real result.
    const auto ticks = scenario_test::write_temp_ticks_csv(256);

    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = ticks.string();

    auto result = fin::app::run_scenario(cfg);
    const double zero = 0.0;
    result.training.mse = zero / zero;
    result.validation_rmse = 1.0 / zero;
    result.metrics.pnl = -1.0 / zero;
    const std::string document = fin::app::scenario_result_to_json(cfg, result);

    // The bug this is about: `"training_mse": nan` is not JSON, whatever jq may accept.
    REQUIRE(document.find("nan") == std::string::npos);
    REQUIRE(document.find("inf") == std::string::npos);
    REQUIRE(document.find("null") != std::string::npos);

    std::string error;
    const auto parsed = fin::app::json::parse(document, error);
    REQUIRE(parsed.has_value());

    std::filesystem::remove(ticks);
}

TEST_CASE("write_number emits null for what JSON cannot spell", "[app][json]")
{
    const double zero = 0.0;

    std::ostringstream finite;
    fin::app::json::write_number(finite, 1.5);
    REQUIRE(finite.str() == "1.5");

    std::ostringstream negative_zero;
    fin::app::json::write_number(negative_zero, -0.0);
    REQUIRE_FALSE(negative_zero.str().empty()); // a real number, however it is spelled

    std::ostringstream nan_out;
    fin::app::json::write_number(nan_out, zero / zero);
    REQUIRE(nan_out.str() == "null");

    std::ostringstream inf_out;
    fin::app::json::write_number(inf_out, 1.0 / zero);
    REQUIRE(inf_out.str() == "null");

    std::ostringstream neg_inf_out;
    fin::app::json::write_number(neg_inf_out, -1.0 / zero);
    REQUIRE(neg_inf_out.str() == "null");
}
