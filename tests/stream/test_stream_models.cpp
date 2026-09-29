#include "catch2_compat.hpp"

#include "TestBacktestHelpers.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "TestTempFiles.hpp"
#include "app/TestScenarioHelpers.hpp"
#include "fin/app/ScenarioRunner.hpp"
#include "fin/backtest/Backtester.hpp"
#include "fin/io/MockTickSource.hpp"
#include "fin/io/Sources.hpp"
#include "fin/ml/LinearModel.hpp"
#include "fin/signal/SignalEngine.hpp"
#include "fin/stream/StreamEngine.hpp"
#include "stream/TestStreamHelpers.hpp"

using fin::stream::ModelResolver;
using fin::stream::StreamConfig;
using fin::stream::StreamEngine;
using fin::stream::SymbolModel;
using fin::stream::SymbolPolicy;
using stream_test::events_for;
using stream_test::make_tick;
using stream_test::Recorded;
using stream_test::RecordingSink;
using stream_test::require_same_events;

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

    // One symbol trained by the batch path, the model taken in memory (see
    // test_stream_matches_batch for why not through a file) with the feature set it resolved.
    struct Trained
    {
        fin::app::ScenarioConfig cfg;
        fin::app::ScenarioResult result;
        SymbolModel model;
    };

    Trained train(const std::filesystem::path &ticks, const std::string &symbol,
                  std::vector<std::string> features = {})
    {
        Trained out;
        out.cfg.ticks_path = ticks.string();
        out.cfg.symbol = symbol;
        out.cfg.features = std::move(features);
        out.result = fin::app::run_scenario(out.cfg);
        out.model.model = std::make_shared<fin::ml::LinearModel>(out.result.training.model);
        out.model.features = out.result.features;
        return out;
    }

    ModelResolver resolver_for(const std::map<std::string, SymbolModel> &models)
    {
        return ModelResolver{[models](const std::string &symbol)
                             {
                                 const auto it = models.find(symbol);
                                 return it == models.end() ? SymbolModel{} : it->second;
                             }};
    }

    backtest_test::SplitMetrics backtest(const std::vector<Recorded> &events, const fin::app::ScenarioConfig &cfg,
                                         long long split_ms)
    {
        std::vector<backtest_test::Bar> bars;
        for (const auto &event : events)
            bars.emplace_back(event.candle, event.prediction);
        return backtest_test::split_backtest(bars, cfg, split_ms);
    }

    std::size_t first_row(const std::vector<Recorded> &events)
    {
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            if (events[i].has_row)
                return i;
        }
        return events.size();
    }
}

TEST_CASE("Each symbol predicts with its own model and its own features", "[stream][models][batch]")
{
    // ABC and XYZ are trained separately, on different feature sets, and each routed symbol
    // has to reproduce its own batch run trade for trade. With one feature set for every
    // pipeline, XYZ's model would be scored on the default six -- LinearModel::predict skips
    // names it does not know -- and its trades would drift.
    const auto ticks = scenario_test::write_two_symbol_ticks();
    const auto abc = train(ticks, "ABC");
    const std::vector<std::string> xyz_features{"close", "rsi", "atr"};
    const auto xyz = train(ticks, "XYZ", xyz_features);
    REQUIRE(abc.result.features != xyz.result.features);

    const std::map<std::string, SymbolModel> models{{"ABC", abc.model}, {"XYZ", xyz.model}};

    fin::io::FileTickSource source(ticks.string());
    RecordingSink sink;
    StreamEngine engine(routed(), resolver_for(models), &sink);
    engine.run(source);
    REQUIRE(engine.has_model("ABC"));
    REQUIRE(engine.has_model("XYZ"));

    for (const Trained *trained : {&abc, &xyz})
    {
        const auto events = events_for(sink.events, trained->cfg.symbol);
        REQUIRE(events.size() == trained->result.candles);
        // Warmup follows the symbol's own feature set: the two differ here.
        REQUIRE(first_row(events) == trained->result.warmup_candles);

        backtest_test::require_matches(backtest(events, trained->cfg, trained->result.out_of_sample_from_ms),
                                       trained->result);

        // And routing stays invisible: the same events as a stream of that symbol alone.
        fin::io::FileTickSource alone_source(ticks.string());
        RecordingSink alone_sink;
        StreamEngine alone(only(trained->cfg.symbol), resolver_for(models), &alone_sink);
        alone.run(alone_source);
        require_same_events(events, alone_sink.events);
    }
    REQUIRE(abc.result.warmup_candles != xyz.result.warmup_candles);

    std::filesystem::remove(ticks);
}

TEST_CASE("A model directory written by run_scenario drives the stream", "[stream][models]")
{
    // The path the CLI takes: run_scenario saves one file per symbol, the directory loader
    // reads them back, and each pipeline gets the feature set its file recorded.
    const auto ticks = scenario_test::write_two_symbol_ticks();
    const test_files::TempDir dir("aiquant_stream_models_");

    for (const auto &[symbol, features] :
         std::map<std::string, std::vector<std::string>>{{"ABC", {}}, {"XYZ", {"close", "rsi", "atr"}}})
    {
        fin::app::ScenarioConfig cfg{};
        cfg.ticks_path = ticks.string();
        cfg.symbol = symbol;
        cfg.features = features;
        cfg.model_output_path = (dir.path() / (symbol + ".csv")).string();
        REQUIRE(fin::app::run_scenario(cfg).model_saved);
    }

    std::unordered_map<std::string, std::shared_ptr<fin::ml::LinearModel>> loaded;
    std::string error;
    REQUIRE(fin::ml::load_linear_model_dir(dir.string(), loaded, error));
    REQUIRE(loaded.size() == 2);

    ModelResolver resolver{[&loaded](const std::string &symbol)
                           {
                               const auto it = loaded.find(symbol);
                               if (it == loaded.end())
                                   return SymbolModel{};
                               return SymbolModel{it->second, it->second->feature_names(), std::nullopt, std::nullopt};
                           }};

    fin::io::FileTickSource source(ticks.string());
    RecordingSink sink;
    StreamEngine engine(routed(), std::move(resolver), &sink);
    engine.run(source);

    const auto by_symbol = engine.stats_by_symbol();
    REQUIRE(by_symbol.size() == 2);
    for (const auto &[symbol, stats] : by_symbol)
    {
        REQUIRE(engine.has_model(symbol));
        REQUIRE(stats.predictions == stats.feature_rows);
        REQUIRE(stats.prediction_errors == 0);
    }
    // XYZ's three features warm up well before ABC's MACD does.
    REQUIRE(first_row(events_for(sink.events, "XYZ")) < first_row(events_for(sink.events, "ABC")));

    std::filesystem::remove(ticks);
}

TEST_CASE("A symbol without a model runs without predictions", "[stream][models]")
{
    const auto ticks = scenario_test::write_two_symbol_ticks();
    const auto abc = train(ticks, "ABC");
    const std::map<std::string, SymbolModel> models{{"ABC", abc.model}};

    fin::io::FileTickSource source(ticks.string());
    RecordingSink sink;
    StreamEngine engine(routed(), resolver_for(models), &sink);
    engine.run(source);

    REQUIRE(engine.has_model("ABC"));
    REQUIRE_FALSE(engine.has_model("XYZ"));
    REQUIRE_FALSE(engine.has_model("NOPE")); // no pipeline at all

    const auto by_symbol = engine.stats_by_symbol();
    REQUIRE(by_symbol[1].first == "XYZ");
    REQUIRE(by_symbol[1].second.candles > 0);
    REQUIRE(by_symbol[1].second.feature_rows > 0);
    REQUIRE(by_symbol[1].second.predictions == 0);
    for (const auto &event : events_for(sink.events, "XYZ"))
        REQUIRE_FALSE(event.prediction.has_value());

    // ABC is not affected by XYZ lacking a model.
    fin::io::FileTickSource alone_source(ticks.string());
    RecordingSink alone_sink;
    StreamEngine alone(only("ABC"), resolver_for(models), &alone_sink);
    alone.run(alone_source);
    require_same_events(events_for(sink.events, "ABC"), alone_sink.events);

    std::filesystem::remove(ticks);
}

TEST_CASE("The resolver is asked once per symbol", "[stream][models]")
{
    std::map<std::string, int> calls;
    ModelResolver counting{[&calls](const std::string &symbol)
                           {
                               ++calls[symbol];
                               return SymbolModel{};
                           }};

    constexpr long long base_ms = 1693492800000LL;
    std::vector<fin::core::Tick> ticks;
    for (int i = 0; i < 5; ++i)
    {
        ticks.push_back(make_tick(base_ms + i * 60000LL, 100.0 + i, "ABC"));
        ticks.push_back(make_tick(base_ms + i * 60000LL + 1, 900.0 + i, "XYZ"));
    }
    fin::io::MockTickSource source(std::move(ticks));
    StreamEngine engine(routed(), std::move(counting));
    engine.run(source);

    REQUIRE(calls.size() == 2);
    REQUIRE(calls["ABC"] == 1);
    REQUIRE(calls["XYZ"] == 1);
}

TEST_CASE("A configured feature set that contradicts the model is refused", "[stream][models]")
{
    // Explicit config.features and a model that recorded a different set cannot both hold;
    // scoring the model on the configured set would use a subset of its weights.
    StreamConfig cfg = routed();
    cfg.features = {"close"};

    const std::vector<std::string> recorded{"close", "rsi"};
    ModelResolver resolver{[&recorded](const std::string &)
                           { return SymbolModel{std::make_shared<fin::ml::LinearModel>(), recorded, std::nullopt, std::nullopt}; }};

    fin::io::MockTickSource source({make_tick(1693492800000LL, 100.0, "ABC")});
    StreamEngine engine(cfg, std::move(resolver));

    std::string message;
    try
    {
        engine.run(source);
    }
    catch (const std::invalid_argument &ex)
    {
        message = ex.what();
    }
    REQUIRE(message.find("ABC") != std::string::npos);
}
