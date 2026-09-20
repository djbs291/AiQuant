#include "catch2_compat.hpp"

#include <filesystem>
#include <memory>
#include <vector>

#include "app/TestScenarioHelpers.hpp"
#include "fin/app/ScenarioRunner.hpp"
#include "fin/backtest/Backtester.hpp"
#include "fin/io/Pipeline.hpp"
#include "fin/io/Sources.hpp"
#include "fin/ml/LinearModel.hpp"
#include "fin/signal/SignalEngine.hpp"
#include "fin/stream/StreamEngine.hpp"
#include "stream/TestStreamHelpers.hpp"

using fin::stream::StreamConfig;
using fin::stream::StreamEngine;
using stream_test::RecordingSink;

// The anchor for the whole streaming layer: the live path and the batch path have to agree
// on the same file, bar for bar and trade for trade. Everything else in fin_stream is only
// worth having if this holds.
TEST_CASE("The streaming path matches the batch path bar for bar", "[stream][batch]")
{
    const auto ticks = scenario_test::write_temp_ticks_csv(256);

    // 1. The batch path, with every default: ridge, the historical six features, no online.
    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = ticks.string();
    const auto result = fin::app::run_scenario(cfg);
    REQUIRE(result.candles > 0);
    REQUIRE(result.feature_rows > 3);

    // 2. The very model the batch replay used, taken in memory. Deliberately not through
    //    save_linear_model: it writes at setprecision(12), and a reload could flip a signal
    //    at a threshold and turn a real equivalence test into a flake.
    auto model = std::make_shared<fin::ml::LinearModel>(result.training.model);

    // 3. The same candles the batch path built, as its own reference.
    const auto batch = fin::io::resample_csv_with_stats(ticks.string(), fin::io::Timeframe::M1, {});
    REQUIRE(batch.candles.size() == result.candles);

    // 4. The streaming path over the same file. A default StreamConfig carries the same
    //    periods and signal rules a default ScenarioConfig does, which is what makes the two
    //    comparable at all.
    fin::io::FileTickSource source(ticks.string());
    RecordingSink sink;
    StreamEngine engine(StreamConfig{}, model, &sink);
    const auto stats = engine.run(source);

    // --- Same candles ---
    REQUIRE(sink.events.size() == batch.candles.size());
    REQUIRE(stats.candles == batch.candles.size());
    REQUIRE(stats.feature_rows == result.feature_rows);

    for (std::size_t i = 0; i < sink.events.size(); ++i)
    {
        const auto &streamed = sink.events[i].candle;
        const auto &reference = batch.candles[i];
        REQUIRE(stream_test::to_ms(streamed.start_time()) == stream_test::to_ms(reference.start_time()));
        REQUIRE(streamed.open().value() == Approx(reference.open().value()).margin(1e-9));
        REQUIRE(streamed.high().value() == Approx(reference.high().value()).margin(1e-9));
        REQUIRE(streamed.low().value() == Approx(reference.low().value()).margin(1e-9));
        REQUIRE(streamed.close().value() == Approx(reference.close().value()).margin(1e-9));
        REQUIRE(streamed.volume().value() == Approx(reference.volume().value()).margin(1e-9));
    }

    // The last bar is the one flush() closed. It does not diverge, because the batch
    // pipeline appends its flushed bar as an ordinary candle too.
    REQUIRE(sink.events.back().partial);

    // Warmup lines up: the first bar carrying a row sits exactly at the warmup boundary.
    std::size_t first_row = sink.events.size();
    for (std::size_t i = 0; i < sink.events.size(); ++i)
    {
        if (sink.events[i].has_row)
        {
            first_row = i;
            break;
        }
    }
    REQUIRE(first_row == result.warmup_candles);

    // --- Same trades ---
    // The tamper-proof half. Feed what the stream dispatched into a fresh Backtester wired
    // exactly as run_scenario wires it: any one-bar shift in the pending prediction, or any
    // drift in the snapshot indicators, changes the trades and fails here.
    fin::signal::SignalEngineConfig scfg{};
    scfg.rsi_buy_below = cfg.rsi_buy;
    scfg.rsi_sell_above = cfg.rsi_sell;
    scfg.use_ema_crossover = cfg.use_ema_crossover;

    fin::backtest::BacktestConfig btcfg{};
    btcfg.ema_fast = cfg.ema_fast;
    btcfg.ema_slow = cfg.ema_slow;
    btcfg.rsi_period = cfg.rsi_period;

    fin::backtest::Backtester bt(btcfg, fin::signal::SignalEngine{scfg});
    for (const auto &event : sink.events)
        bt.on_candle(event.candle, event.prediction);
    const auto metrics = bt.finalize();

    REQUIRE(metrics.trades == result.metrics.trades);
    REQUIRE(metrics.wins == result.metrics.wins);
    REQUIRE(metrics.losses == result.metrics.losses);
    REQUIRE(metrics.final_cash == Approx(result.metrics.final_cash).margin(1e-9));
    REQUIRE(metrics.pnl == Approx(result.metrics.pnl).margin(1e-9));
    REQUIRE(metrics.return_pct == Approx(result.metrics.return_pct).margin(1e-9));
    REQUIRE(metrics.max_drawdown == Approx(result.metrics.max_drawdown).margin(1e-9));

    std::filesystem::remove(ticks);
}
