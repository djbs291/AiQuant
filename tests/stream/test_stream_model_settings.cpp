#include "catch2_compat.hpp"

#include "TestBacktestHelpers.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "TestTempFiles.hpp"
#include "app/TestScenarioHelpers.hpp"
#include "fin/app/ScenarioRunner.hpp"
#include "fin/backtest/Backtester.hpp"
#include "fin/io/Sources.hpp"
#include "fin/ml/LinearModel.hpp"
#include "fin/signal/SignalEngine.hpp"
#include "fin/stream/StreamEngine.hpp"
#include "fin/stream/SymbolModel.hpp"
#include "stream/TestStreamHelpers.hpp"

using fin::stream::ModelOverrides;
using fin::stream::ModelResolver;
using fin::stream::StreamConfig;
using fin::stream::StreamEngine;
using fin::stream::SymbolModel;
using stream_test::RecordingSink;

namespace
{
    // Trained by the batch path on settings a default StreamConfig does not share: M5 candles,
    // rsi 10, atr 7.
    fin::app::ScenarioConfig off_default_config(const std::filesystem::path &ticks)
    {
        fin::app::ScenarioConfig cfg{};
        cfg.ticks_path = ticks.string();
        cfg.timeframe = fin::io::Timeframe::M5;
        cfg.features = {"close", "rsi", "atr"};
        cfg.rsi_period = 10;
        cfg.atr_period = 7;
        return cfg;
    }

    std::shared_ptr<fin::ml::LinearModel> recorded(std::vector<std::pair<std::string, double>> params,
                                                   std::string timeframe)
    {
        auto model = std::make_shared<fin::ml::LinearModel>();
        model->set_named_weights({{"close", 1.0}, {"rsi", 0.5}}, 0.0);
        model->set_training_params(std::move(params));
        model->set_timeframe(std::move(timeframe));
        return model;
    }
}

TEST_CASE("run_scenario records the parameters and timeframe it trained with", "[stream][params][app]")
{
    const auto ticks = scenario_test::write_temp_ticks_csv(2000);
    const test_files::TempDir dir("aiquant_params_run_");
    auto cfg = off_default_config(ticks);
    cfg.model_output_path = (dir.path() / "m.csv").string();
    const auto result = fin::app::run_scenario(cfg);

    const std::vector<std::pair<std::string, double>> expected{{"rsi", 10.0}, {"atr", 7.0}};
    REQUIRE(result.training.model.training_params() == expected);
    REQUIRE(result.training.model.timeframe() == "M5");

    std::ifstream in(*cfg.model_output_path);
    std::ostringstream text;
    text << in.rdbuf();
    REQUIRE(text.str().find("# params: rsi=10,atr=7\n") != std::string::npos);
    REQUIRE(text.str().find("# timeframe: M5\n") != std::string::npos);

    std::filesystem::remove(ticks);
}

TEST_CASE("A stream on a model's recorded settings matches the batch run that trained it", "[stream][params][batch]")
{
    // The StreamConfig is left at its defaults -- M1, rsi 14, atr 14 -- and the model's own
    // settings have to win. Without them the candles are M1 and the counts diverge at once.
    const auto ticks = scenario_test::write_temp_ticks_csv(2000);
    const auto cfg = off_default_config(ticks);
    const auto result = fin::app::run_scenario(cfg);

    auto model = std::make_shared<fin::ml::LinearModel>(result.training.model);
    SymbolModel settings;
    std::string error;
    REQUIRE(fin::stream::symbol_model_from(model, ModelOverrides{}, {}, settings, error));
    // In memory the model has no recorded feature names (only a loaded file does), so give
    // the ones the run resolved -- the anchor test's reason for staying in memory applies.
    settings.features = result.features;

    fin::io::FileTickSource source(ticks.string());
    RecordingSink sink;
    StreamEngine engine(StreamConfig{}, ModelResolver{[&settings](const std::string &) { return settings; }}, &sink);
    const auto stats = engine.run(source);

    REQUIRE(stats.candles == result.candles);
    REQUIRE(stats.feature_rows == result.feature_rows);

    std::vector<backtest_test::Bar> bars;
    for (const auto &event : sink.events)
        bars.emplace_back(event.candle, event.prediction);
    backtest_test::require_matches(backtest_test::split_backtest(bars, cfg, result.out_of_sample_from_ms), result);

    std::filesystem::remove(ticks);
}

TEST_CASE("symbol_model_from adopts what the file records", "[stream][params]")
{
    SymbolModel out;
    std::string error;
    fin::indicators::FeatureParams base{};
    base.momentum = 5; // not recorded, so it stays
    REQUIRE(fin::stream::symbol_model_from(recorded({{"rsi", 10.0}, {"atr", 7.0}}, "M5"), ModelOverrides{}, base, out, error));
    REQUIRE(out.params.has_value());
    REQUIRE(out.params->rsi == 10);
    REQUIRE(out.params->atr == 7);
    REQUIRE(out.params->momentum == 5);
    REQUIRE(out.timeframe == fin::io::Timeframe::M5);
    REQUIRE(out.model != nullptr);
}

TEST_CASE("symbol_model_from refuses an override the file contradicts", "[stream][params]")
{
    SymbolModel out;
    std::string error;

    ModelOverrides rsi;
    rsi.params = {{"rsi", 14.0}};
    REQUIRE_FALSE(fin::stream::symbol_model_from(recorded({{"rsi", 10.0}}, "M1"), rsi, {}, out, error));
    REQUIRE(error.find("rsi 14") != std::string::npos);
    REQUIRE(error.find("rsi = 10") != std::string::npos);

    ModelOverrides tf;
    tf.timeframe = fin::io::Timeframe::M5;
    error.clear();
    REQUIRE_FALSE(fin::stream::symbol_model_from(recorded({}, "M1"), tf, {}, out, error));
    REQUIRE(error.find("M5") != std::string::npos);
    REQUIRE(error.find("M1") != std::string::npos);

    // Recorded feature names only come from a file.
    auto with_features = std::make_shared<fin::ml::LinearModel>();
    const test_files::TempDir dir("aiquant_params_features_");
    const auto path = dir.write("m.csv", "# features: close,rsi\nbias,0\nclose,1\nrsi,1\n");
    REQUIRE(with_features->load_from_file(path.string()));
    ModelOverrides features;
    features.features = {"close"};
    error.clear();
    REQUIRE_FALSE(fin::stream::symbol_model_from(with_features, features, {}, out, error));
    REQUIRE(error.find("close,rsi") != std::string::npos);
}

TEST_CASE("symbol_model_from accepts an override that agrees, and a file that records nothing", "[stream][params]")
{
    SymbolModel out;
    std::string error;

    ModelOverrides same;
    same.params = {{"rsi", 10.0}};
    same.timeframe = fin::io::Timeframe::M1;
    REQUIRE(fin::stream::symbol_model_from(recorded({{"rsi", 10.0}}, "M1"), same, {}, out, error));
    REQUIRE(out.params->rsi == 10);

    // A file from before any of this: the overrides and the base are all there is.
    ModelOverrides typed;
    typed.params = {{"rsi", 21.0}};
    typed.timeframe = fin::io::Timeframe::H1;
    REQUIRE(fin::stream::symbol_model_from(recorded({}, ""), typed, {}, out, error));
    REQUIRE(out.params->rsi == 21);
    REQUIRE(out.timeframe == fin::io::Timeframe::H1);
    REQUIRE(out.features.empty());
}
