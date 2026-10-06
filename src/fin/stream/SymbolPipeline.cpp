#include "fin/stream/SymbolPipeline.hpp"

#include <exception>
#include <utility>
#include <memory>
#include <optional>
#include <vector>

#include "fin/ml/FeatureVector.hpp"

namespace fin::stream
{
    namespace
    {
        const std::vector<std::string> &resolve_features(const StreamConfig &config)
        {
            return config.features.empty() ? fin::indicators::default_feature_names()
                                           : config.features;
        }
    } // namespace

    SymbolPipeline::SymbolPipeline(std::string symbol, const StreamConfig &config,
                                   std::shared_ptr<fin::ml::IModel> model, ISignalSink *sink)
        : symbol_(std::move(symbol)),
          resampler_(config.timeframe),
          bus_(resolve_features(config), config.params),
          ema_fast_(config.params.ema_fast),
          ema_slow_(config.params.ema_slow),
          rsi_(config.params.rsi),
          engine_(config.signal),
          model_(std::move(model)),
          sink_(sink)
    {
    }

    void SymbolPipeline::on_tick(const fin::core::Tick &tick)
    {
        ++stats_.ticks;

        // The resampler drops an out-of-order tick silently, by a stated MVP policy. Checking
        // here first is what turns that silence into a number the caller can read.
        if (last_tick_ts_ && tick.timestamp() < *last_tick_ts_)
        {
            ++stats_.ticks_out_of_order;
            return;
        }
        last_tick_ts_ = tick.timestamp();

        if (auto candle = resampler_.update(tick))
            on_candle(*candle, false);
    }

    void SymbolPipeline::flush()
    {
        if (auto candle = resampler_.flush())
            on_candle(*candle, true);
    }

    void SymbolPipeline::on_candle(const fin::core::Candle &candle, bool partial)
    {
        ++stats_.candles;

        // 1. The snapshot indicators, wired exactly as Backtester::on_candle wires them.
        ema_fast_.update(candle);
        ema_slow_.update(candle);
        rsi_.update(candle);

        fin::signal::IndicatorsSnapshot snapshot{};
        snapshot.ts = candle.start_time();
        // The first place in the engine where this is actually populated: the batch path has
        // no symbol at Candle level and leaves it empty.
        snapshot.symbol = symbol_;
        snapshot.close = candle.close().value();
        snapshot.ema_fast = ema_fast_.is_ready() ? std::optional<double>(ema_fast_.value()) : std::nullopt;
        snapshot.ema_slow = ema_slow_.is_ready() ? std::optional<double>(ema_slow_.value()) : std::nullopt;
        snapshot.rsi = rsi_.is_ready() ? std::optional<double>(rsi_.value()) : std::nullopt;

        // 2. This bar's features, and the model's prediction from them: the forecast of the
        //    move to the next close, made at this close. run_scenario does the same.
        std::optional<fin::indicators::FeatureRow> row = bus_.update(candle);
        std::optional<double> prediction;
        if (row)
        {
            ++stats_.feature_rows;
            if (model_)
            {
                try
                {
                    prediction = model_->predict(fin::ml::FeatureVector::from_feature_row(*row));
                    ++stats_.predictions;
                }
                catch (const std::exception &)
                {
                    // A live feed must not die because one bar upset the model; cmd_backtest
                    // takes the same posture. The bar is simply judged without one.
                    ++stats_.prediction_errors;
                }
            }
        }

        // 3. Judge this bar with that prediction, as the indicator rules judge it with this
        //    bar's EMA and RSI. Until 2026-09 the prediction was held back one bar, so the
        //    model voted on a move that had already happened by the time it was traded on.
        const fin::signal::Signal signal = engine_.eval(snapshot, prediction);
        ++stats_.signals;
        if (signal.model_decisive)
            ++stats_.model_decisive;
        switch (signal.type)
        {
        case fin::signal::SignalType::Buy:
            ++stats_.buys;
            break;
        case fin::signal::SignalType::Sell:
            ++stats_.sells;
            break;
        case fin::signal::SignalType::Hold:
            ++stats_.holds;
            break;
        }

        if (sink_)
        {
            const StreamEvent event{symbol_, candle, row ? &*row : nullptr, prediction, signal, partial};
            sink_->on_signal(event);
        }
    }

} // namespace fin::stream
