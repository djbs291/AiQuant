#include "catch2_compat.hpp"

#include "TestBacktestHelpers.hpp"

#include <filesystem>
#include <optional>

#include "app/TestScenarioHelpers.hpp"
#include "fin/app/ScenarioRunner.hpp"
#include "fin/backtest/Backtester.hpp"
#include "fin/indicators/FeatureBus.hpp"
#include "fin/io/Pipeline.hpp"
#include "fin/ml/FeatureVector.hpp"
#include "fin/signal/SignalEngine.hpp"

namespace
{
    // The plainest statement of when a prediction is used, written out without the stream:
    // compute a candle's features, predict from them, judge that same candle. This is the
    // loop `aiquant backtest --model-linear` runs.
    backtest_test::SplitMetrics predict_then_judge(const fin::app::ScenarioConfig &cfg,
                                              const fin::app::ScenarioResult &result)
    {
        const auto candles = fin::io::resample_csv_with_stats(cfg.ticks_path, cfg.timeframe, {}).candles;

        fin::indicators::FeatureParams params{};
        params.ema_fast = cfg.ema_fast;
        params.ema_slow = cfg.ema_slow;
        params.rsi = cfg.rsi_period;
        params.macd_fast = cfg.macd_fast;
        params.macd_slow = cfg.macd_slow;
        params.macd_signal = cfg.macd_signal;
        params.atr = cfg.atr_period;
        fin::indicators::FeatureBus bus(result.features, params);

        std::vector<backtest_test::Bar> bars;
        for (const auto &candle : candles)
        {
            std::optional<double> prediction;
            if (auto row = bus.update(candle))
                prediction = result.training.model.predict(fin::ml::FeatureVector::from_feature_row(*row));
            bars.emplace_back(candle, prediction);
        }
        return backtest_test::split_backtest(bars, cfg, result.out_of_sample_from_ms);
    }
}

TEST_CASE("run_scenario judges each candle with the prediction made on it", "[app][timing]")
{
    // The model forecasts the move from a candle's close to the next one, and the backtester
    // trades at that close, so that is the candle to act on the forecast -- the indicator
    // rules are judged on it too. run_scenario used to hold the prediction back one candle,
    // voting on a move that had already happened, and so disagreed with `aiquant backtest`
    // for the same model. Two feature sets, so the agreement is not one model's accident.
    const auto ticks = scenario_test::write_temp_ticks_csv(600);

    for (const bool wide : {false, true})
    {
        fin::app::ScenarioConfig cfg{};
        cfg.ticks_path = ticks.string();
        if (wide)
            cfg.features = {"close", "ema_fast", "rsi", "atr"};

        const auto result = fin::app::run_scenario(cfg);
        backtest_test::require_matches(predict_then_judge(cfg, result), result);
    }

    std::filesystem::remove(ticks);
}
