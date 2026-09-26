#include "catch2_compat.hpp"

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/TestScenarioHelpers.hpp"
#include "fin/app/ScenarioRunner.hpp"
#include "fin/backtest/Backtester.hpp"
#include "fin/io/MockTickSource.hpp"
#include "fin/io/Sources.hpp"
#include "fin/ml/LinearModel.hpp"
#include "fin/signal/SignalEngine.hpp"
#include "fin/stream/StreamEngine.hpp"
#include "stream/TestStreamHelpers.hpp"

using fin::stream::StreamConfig;
using fin::stream::StreamEngine;
using fin::stream::SymbolPolicy;
using stream_test::make_tick;
using stream_test::Recorded;
using stream_test::RecordingSink;

namespace
{
    StreamConfig routed()
    {
        StreamConfig cfg{};
        cfg.foreign_symbol = SymbolPolicy::Route;
        return cfg;
    }

    StreamConfig only(const std::string &symbol)
    {
        StreamConfig cfg{};
        cfg.symbol = symbol;
        cfg.foreign_symbol = SymbolPolicy::Skip;
        return cfg;
    }

    std::vector<Recorded> events_for(const std::vector<Recorded> &events, const std::string &symbol)
    {
        std::vector<Recorded> out;
        for (const auto &event : events)
        {
            if (event.symbol == symbol)
                out.push_back(event);
        }
        return out;
    }

    // Exact equality on purpose, predictions included: both runs do the same arithmetic in
    // the same order, so any difference at all means state leaked between pipelines.
    void require_same_events(const std::vector<Recorded> &a, const std::vector<Recorded> &b)
    {
        REQUIRE(a.size() == b.size());
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            REQUIRE(a[i].ts_ms == b[i].ts_ms);
            REQUIRE(a[i].candle.open().value() == b[i].candle.open().value());
            REQUIRE(a[i].candle.high().value() == b[i].candle.high().value());
            REQUIRE(a[i].candle.low().value() == b[i].candle.low().value());
            REQUIRE(a[i].candle.close().value() == b[i].candle.close().value());
            REQUIRE(a[i].candle.volume().value() == b[i].candle.volume().value());
            REQUIRE(a[i].has_row == b[i].has_row);
            REQUIRE(a[i].prediction == b[i].prediction);
            REQUIRE(a[i].type == b[i].type);
            REQUIRE(a[i].partial == b[i].partial);
        }
    }

    // A model trained by the batch path on ABC alone, taken in memory for the reason given in
    // test_stream_matches_batch: a save/reload round trip could flip a signal at a threshold.
    struct TrainedOnAbc
    {
        fin::app::ScenarioConfig cfg;
        fin::app::ScenarioResult result;
        std::shared_ptr<fin::ml::LinearModel> model;
    };

    TrainedOnAbc train_on_abc(const std::filesystem::path &ticks)
    {
        TrainedOnAbc out;
        out.cfg.ticks_path = ticks.string();
        out.cfg.symbol = "ABC";
        out.result = fin::app::run_scenario(out.cfg);
        out.model = std::make_shared<fin::ml::LinearModel>(out.result.training.model);
        return out;
    }
}

TEST_CASE("Routing gives each symbol exactly the stream it would have alone", "[stream][route]")
{
    // The property that makes routing safe: a pipeline cannot tell whether other symbols
    // share its feed. Candles, warmup, the pending prediction and the signal all have to match
    // a run that filtered the file down to that one symbol, with a real model in the loop so a
    // prediction leaking from one pipeline into the next would show.
    const auto ticks = scenario_test::write_two_symbol_ticks();
    const auto trained = train_on_abc(ticks);

    fin::io::FileTickSource routed_source(ticks.string());
    RecordingSink routed_sink;
    StreamEngine routed_engine(routed(), trained.model, &routed_sink);
    routed_engine.run(routed_source);

    for (const std::string symbol : {"ABC", "XYZ"})
    {
        fin::io::FileTickSource alone_source(ticks.string());
        RecordingSink alone_sink;
        StreamEngine alone(only(symbol), trained.model, &alone_sink);
        alone.run(alone_source);

        REQUIRE_FALSE(alone_sink.events.empty());
        require_same_events(events_for(routed_sink.events, symbol), alone_sink.events);
    }

    std::filesystem::remove(ticks);
}

TEST_CASE("A routed symbol trades exactly as the batch path does on that symbol", "[stream][route][batch]")
{
    // The anchor test ties one symbol's stream to run_scenario. This ties a routed stream to
    // it directly, on a file holding two instruments: ABC's events, fed through a Backtester
    // wired as run_scenario wires it, have to reproduce the batch run with symbol = ABC.
    const auto ticks = scenario_test::write_two_symbol_ticks();
    const auto trained = train_on_abc(ticks);
    REQUIRE(trained.result.ticks_other_symbol > 0); // the batch run did see, and skip, XYZ

    fin::io::FileTickSource source(ticks.string());
    RecordingSink sink;
    StreamEngine engine(routed(), trained.model, &sink);
    engine.run(source);

    const auto abc = events_for(sink.events, "ABC");
    REQUIRE(abc.size() == trained.result.candles);

    fin::signal::SignalEngineConfig scfg{};
    scfg.rsi_buy_below = trained.cfg.rsi_buy;
    scfg.rsi_sell_above = trained.cfg.rsi_sell;
    scfg.use_ema_crossover = trained.cfg.use_ema_crossover;

    fin::backtest::BacktestConfig btcfg{};
    btcfg.ema_fast = trained.cfg.ema_fast;
    btcfg.ema_slow = trained.cfg.ema_slow;
    btcfg.rsi_period = trained.cfg.rsi_period;

    fin::backtest::Backtester bt(btcfg, fin::signal::SignalEngine{scfg});
    for (const auto &event : abc)
        bt.on_candle(event.candle, event.prediction);
    const auto metrics = bt.finalize();

    REQUIRE(metrics.trades == trained.result.metrics.trades);
    REQUIRE(metrics.wins == trained.result.metrics.wins);
    REQUIRE(metrics.losses == trained.result.metrics.losses);
    REQUIRE(metrics.final_cash == Approx(trained.result.metrics.final_cash).margin(1e-9));
    REQUIRE(metrics.max_drawdown == Approx(trained.result.metrics.max_drawdown).margin(1e-9));

    std::filesystem::remove(ticks);
}

TEST_CASE("Routed counters add up, symbol by symbol", "[stream][route]")
{
    // Three symbols, first seen in the order C, A, B, with a different number of ticks each.
    constexpr long long base_ms = 1693492800000LL;
    std::vector<fin::core::Tick> ticks;
    for (int i = 0; i < 6; ++i)
    {
        const long long minute = base_ms + static_cast<long long>(i) * 60000;
        ticks.push_back(make_tick(minute, 300.0 + i, "C"));
        ticks.push_back(make_tick(minute + 1, 100.0 + i, "A"));
        if (i % 2 == 0)
            ticks.push_back(make_tick(minute + 2, 200.0 + i, "B"));
    }

    fin::io::MockTickSource source(std::move(ticks));
    RecordingSink sink;
    StreamEngine engine(routed(), nullptr, &sink);
    const auto total = engine.run(source);

    // First appearance, not alphabetical and not the map's order: output has to be the same
    // for the same file every time.
    const std::vector<std::string> first_seen{"C", "A", "B"};
    REQUIRE(engine.symbols() == first_seen);
    REQUIRE(engine.bound_symbol().empty());

    const auto by_symbol = engine.stats_by_symbol();
    REQUIRE(by_symbol.size() == 3);
    REQUIRE(by_symbol[0].second.ticks == 6);
    REQUIRE(by_symbol[1].second.ticks == 6);
    REQUIRE(by_symbol[2].second.ticks == 3);

    // Every counter: the engine's total has to be the plain sum of its pipelines. (A counter
    // added to StreamStats later is caught by a static_assert beside accumulate(), not here.)
    fin::stream::StreamStats sum{};
    for (const auto &[symbol, s] : by_symbol)
    {
        sum.ticks += s.ticks;
        sum.ticks_out_of_order += s.ticks_out_of_order;
        sum.candles += s.candles;
        sum.feature_rows += s.feature_rows;
        sum.predictions += s.predictions;
        sum.prediction_errors += s.prediction_errors;
        sum.signals += s.signals;
        sum.buys += s.buys;
        sum.sells += s.sells;
        sum.holds += s.holds;
    }
    REQUIRE(total.ticks == 15);
    REQUIRE(total.ticks == sum.ticks);
    REQUIRE(total.ticks_out_of_order == sum.ticks_out_of_order);
    REQUIRE(total.candles == sum.candles);
    REQUIRE(total.feature_rows == sum.feature_rows);
    REQUIRE(total.predictions == sum.predictions);
    REQUIRE(total.prediction_errors == sum.prediction_errors);
    REQUIRE(total.signals == sum.signals);
    REQUIRE(total.buys == sum.buys);
    REQUIRE(total.sells == sum.sells);
    REQUIRE(total.holds == sum.holds);
    REQUIRE(total.ticks_other_symbol == 0); // routing drops nothing

    // flush() closed one partial bar per symbol, in the same first-appearance order.
    std::vector<std::string> partial_order;
    for (const auto &event : sink.events)
    {
        if (event.partial)
            partial_order.push_back(event.symbol);
    }
    REQUIRE(partial_order == first_seen);
}

TEST_CASE("Tick order is judged per symbol when routing", "[stream][route]")
{
    // XYZ's tick is earlier than the ABC tick before it, which a single shared clock would
    // call out of order. Each pipeline keeps its own clock, so neither tick is dropped.
    constexpr long long base_ms = 1693492800000LL;
    fin::io::MockTickSource source({make_tick(base_ms + 5000, 100.0, "ABC"),
                                    make_tick(base_ms + 1000, 900.0, "XYZ"),
                                    make_tick(base_ms + 6000, 101.0, "ABC"),
                                    make_tick(base_ms + 500, 899.0, "XYZ")});
    StreamEngine engine(routed());
    const auto stats = engine.run(source);

    REQUIRE(stats.ticks == 4);
    // Only the last one is out of order, and only against XYZ's own previous tick.
    REQUIRE(stats.ticks_out_of_order == 1);
    REQUIRE(engine.stats_by_symbol()[1].second.ticks_out_of_order == 1);
}

TEST_CASE("Route and a named symbol cannot be combined", "[stream][route]")
{
    StreamConfig cfg = routed();
    cfg.symbol = "ABC";

    bool refused = false;
    try
    {
        StreamEngine engine(cfg);
    }
    catch (const std::invalid_argument &)
    {
        refused = true;
    }
    REQUIRE(refused);
}

TEST_CASE("The single-symbol refusal points at routing", "[stream][route]")
{
    fin::io::MockTickSource source({make_tick(1693492800000LL, 100.0, "ABC"),
                                    make_tick(1693492801000LL, 900.0, "XYZ")});
    StreamEngine engine(StreamConfig{});

    std::string message;
    try
    {
        engine.run(source);
    }
    catch (const std::invalid_argument &ex)
    {
        message = ex.what();
    }
    REQUIRE(message.find("--per-symbol") != std::string::npos);
}
