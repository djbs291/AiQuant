#include "catch2_compat.hpp"

#include <filesystem>
#include <string>

#include "app/TestScenarioHelpers.hpp"
#include "fin/app/ScenarioRunner.hpp"
#include "fin/app/ScenarioSerialization.hpp"
#include "fin/backtest/Backtester.hpp"
#include "fin/io/Pipeline.hpp"

TEST_CASE("The headline backtest covers only the candles the model was not trained on", "[app][oos]")
{
    // It used to replay every candle with the trained model, so about train_ratio of each
    // reported trade and PnL came from the rows the model had been fitted to.
    const auto ticks = scenario_test::write_temp_ticks_csv(600);
    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = ticks.string();
    const auto result = fin::app::run_scenario(cfg);

    // Every candle lands on exactly one side of the split.
    REQUIRE(result.in_sample_candles + result.out_of_sample_candles == result.candles);
    // After warmup each candle yields one feature row, so the out-of-sample stretch is exactly
    // the validation rows: the ones validation_rmse scores, plus the last, which has no target.
    REQUIRE(result.out_of_sample_candles == result.validation_samples + 1);
    // And it starts on the candle of the first of them. The trainer fits `samples` rows, and
    // the next row's close is the last target it sees -- but not that row's features, so the
    // prediction made on it forecasts a close the model has never been shown.
    REQUIRE(result.in_sample_candles == result.warmup_candles + result.training.samples);

    // Both sets of metrics go out, labelled; "metrics" is the out-of-sample one.
    const std::string json = fin::app::scenario_result_to_json(cfg, result);
    REQUIRE(json.find("\"metrics_in_sample\"") != std::string::npos);
    REQUIRE(json.find("\"out_of_sample_candles\": " + std::to_string(result.out_of_sample_candles)) != std::string::npos);
    REQUIRE(json.find("\"out_of_sample_from_ms\": " + std::to_string(result.out_of_sample_from_ms)) != std::string::npos);

    std::filesystem::remove(ticks);
}

TEST_CASE("Backtester::observe warms the indicators and never trades", "[backtest][oos]")
{
    const auto ticks = scenario_test::write_temp_ticks_csv(200);
    const auto candles = fin::io::resample_csv_with_stats(ticks.string(), fin::io::Timeframe::M1, {}).candles;
    REQUIRE(candles.size() > 100);

    fin::backtest::Backtester watcher;
    for (const auto &candle : candles)
        watcher.observe(candle);
    const auto watched = watcher.finalize();
    REQUIRE(watched.trades == 0);
    REQUIRE(watched.pnl == Approx(0.0).margin(1e-12));
    REQUIRE(watched.max_drawdown == Approx(0.0).margin(1e-12));

    // Observing the first half and trading the second must behave, on the second half,
    // exactly like a backtester that traded all along but held no position at the split --
    // which is what a fresh one that observed nothing cannot do, because its EMA and RSI
    // would still be cold there.
    const std::size_t half = candles.size() / 2;
    fin::backtest::Backtester warmed;
    fin::backtest::Backtester cold;
    for (std::size_t i = 0; i < half; ++i)
        warmed.observe(candles[i]);
    for (std::size_t i = half; i < candles.size(); ++i)
    {
        warmed.on_candle(candles[i]);
        cold.on_candle(candles[i]);
    }
    const auto warm_metrics = warmed.finalize();
    const auto cold_metrics = cold.finalize();
    REQUIRE(warm_metrics.trades > 0);
    // The cold one spends its first candles warming up and so trades differently; if the two
    // agreed, observe() would not be doing anything.
    REQUIRE((warm_metrics.trades != cold_metrics.trades ||
             warm_metrics.final_cash != Approx(cold_metrics.final_cash).margin(1e-9)));

    std::filesystem::remove(ticks);
}
