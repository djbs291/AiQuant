#pragma once

// Replays candles through a backtest the way run_scenario does, so a test can hold a stream,
// or a hand-written loop, to the batch run's two sets of metrics.

#include "catch2_compat.hpp"

#include <chrono>
#include <optional>
#include <utility>
#include <vector>

#include "fin/app/ScenarioRunner.hpp"
#include "fin/backtest/Backtester.hpp"
#include "fin/core/Candle.hpp"
#include "fin/signal/SignalEngine.hpp"

namespace backtest_test
{
    // A candle and the prediction it was judged with.
    using Bar = std::pair<fin::core::Candle, std::optional<double>>;

    struct SplitMetrics
    {
        fin::backtest::Metrics out_of_sample;
        fin::backtest::Metrics in_sample;
    };

    // Configured from `cfg` exactly as run_scenario configures its backtesters. Bars before
    // `split_ms` trade in the in-sample backtest and only warm the out-of-sample one; bars
    // from `split_ms` on trade in the out-of-sample one.
    inline SplitMetrics split_backtest(const std::vector<Bar> &bars, const fin::app::ScenarioConfig &cfg,
                                       long long split_ms)
    {
        fin::signal::SignalEngineConfig scfg{};
        scfg.rsi_buy_below = cfg.rsi_buy;
        scfg.rsi_sell_above = cfg.rsi_sell;
        scfg.use_ema_crossover = cfg.use_ema_crossover;
        scfg.model_weight = cfg.model_weight;

        fin::backtest::BacktestConfig btcfg{};
        if (cfg.initial_cash)
            btcfg.initial_cash = *cfg.initial_cash;
        if (cfg.trade_qty)
            btcfg.trade_qty = *cfg.trade_qty;
        if (cfg.fee_per_trade)
            btcfg.fee_per_trade = *cfg.fee_per_trade;
        btcfg.ema_fast = cfg.ema_fast;
        btcfg.ema_slow = cfg.ema_slow;
        btcfg.rsi_period = cfg.rsi_period;

        fin::backtest::Backtester in_sample(btcfg, fin::signal::SignalEngine{scfg});
        fin::backtest::Backtester out_of_sample(btcfg, fin::signal::SignalEngine{scfg});
        for (const auto &[candle, prediction] : bars)
        {
            using namespace std::chrono;
            const long long ms = duration_cast<milliseconds>(candle.start_time().time_since_epoch()).count();
            if (ms >= split_ms)
            {
                out_of_sample.on_candle(candle, prediction);
            }
            else
            {
                in_sample.on_candle(candle, prediction);
                out_of_sample.observe(candle);
            }
        }
        return {out_of_sample.finalize(), in_sample.finalize()};
    }

    inline void require_same_metrics(const fin::backtest::Metrics &got, const fin::backtest::Metrics &expected)
    {
        REQUIRE(got.trades == expected.trades);
        REQUIRE(got.wins == expected.wins);
        REQUIRE(got.losses == expected.losses);
        REQUIRE(got.model_decisive_signals == expected.model_decisive_signals);
        REQUIRE(got.final_cash == Approx(expected.final_cash).margin(1e-9));
        REQUIRE(got.pnl == Approx(expected.pnl).margin(1e-9));
        REQUIRE(got.return_pct == Approx(expected.return_pct).margin(1e-9));
        REQUIRE(got.max_drawdown == Approx(expected.max_drawdown).margin(1e-9));
    }

    // Both halves at once: the out-of-sample headline and the in-sample comparison.
    inline void require_matches(const SplitMetrics &got, const fin::app::ScenarioResult &result)
    {
        REQUIRE(result.out_of_sample_candles > 0);
        require_same_metrics(got.out_of_sample, result.metrics);
        require_same_metrics(got.in_sample, result.metrics_in_sample);
    }
}
