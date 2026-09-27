#include "catch2_compat.hpp"

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
    fin::backtest::Metrics predict_then_judge(const fin::app::ScenarioConfig &cfg,
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

        fin::signal::SignalEngineConfig scfg{};
        scfg.rsi_buy_below = cfg.rsi_buy;
        scfg.rsi_sell_above = cfg.rsi_sell;
        scfg.use_ema_crossover = cfg.use_ema_crossover;
        fin::backtest::BacktestConfig btcfg{};
        btcfg.ema_fast = cfg.ema_fast;
        btcfg.ema_slow = cfg.ema_slow;
        btcfg.rsi_period = cfg.rsi_period;
        fin::backtest::Backtester bt(btcfg, fin::signal::SignalEngine{scfg});

        for (const auto &candle : candles)
        {
            std::optional<double> prediction;
            if (auto row = bus.update(candle))
                prediction = result.training.model.predict(fin::ml::FeatureVector::from_feature_row(*row));
            bt.on_candle(candle, prediction);
        }
        return bt.finalize();
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
        const auto expected = predict_then_judge(cfg, result);

        REQUIRE(result.metrics.trades == expected.trades);
        REQUIRE(result.metrics.wins == expected.wins);
        REQUIRE(result.metrics.losses == expected.losses);
        REQUIRE(result.metrics.final_cash == Approx(expected.final_cash).margin(1e-9));
        REQUIRE(result.metrics.max_drawdown == Approx(expected.max_drawdown).margin(1e-9));
    }

    std::filesystem::remove(ticks);
}
