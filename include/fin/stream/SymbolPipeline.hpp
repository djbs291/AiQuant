#pragma once
#ifndef FIN_STREAM_SYMBOL_PIPELINE_HPP
#define FIN_STREAM_SYMBOL_PIPELINE_HPP

#include <memory>
#include <optional>
#include <string>

#include "fin/core/Candle.hpp"
#include "fin/core/Tick.hpp"
#include "fin/core/Timestamp.hpp"
#include "fin/indicators/FeatureBus.hpp"
#include "fin/indicators/adapters/CandleAdapters.hpp"
#include "fin/io/Resampler.hpp"
#include "fin/ml/IModel.hpp"
#include "fin/signal/SignalEngine.hpp"
#include "fin/stream/SignalSink.hpp"
#include "fin/stream/StreamConfig.hpp"

namespace fin::stream
{
    /**
     * @brief Every stage, and all the state, for one symbol.
     *
     * Deliberately self-contained: it holds no reference to a source and touches no global
     * state, so a worker thread can own one outright when per-symbol workers arrive. Keep it
     * that way — it is the whole reason routing lives in StreamEngine instead of here.
     */
    class SymbolPipeline
    {
    public:
        SymbolPipeline(std::string symbol, const StreamConfig &config,
                       std::shared_ptr<fin::ml::IModel> model, ISignalSink *sink);

        void on_tick(const fin::core::Tick &tick);

        // Closes the partial candle through the same path, flagged as partial.
        void flush();

        [[nodiscard]] const std::string &symbol() const noexcept { return symbol_; }
        [[nodiscard]] const StreamStats &stats() const noexcept { return stats_; }

    private:
        void on_candle(const fin::core::Candle &candle, bool partial);

        std::string symbol_;
        fin::io::TickToCandleResampler resampler_;
        fin::indicators::FeatureBus bus_;

        // Mirrors Backtester::on_candle, which is the batch path's source of truth for the
        // snapshot the SignalEngine sees. Duplicated rather than extracted because the only
        // natural home for a shared builder would force fin_signal to depend on
        // fin_indicators, inverting the documented layering. If the two ever drift, the
        // stream/batch equivalence test fails on the final metrics.
        fin::indicators::EMAFromCandle ema_fast_;
        fin::indicators::EMAFromCandle ema_slow_;
        fin::indicators::RSIFromCandle rsi_;

        fin::signal::SignalEngine engine_;
        std::shared_ptr<fin::ml::IModel> model_;
        ISignalSink *sink_ = nullptr;

        std::optional<double> pending_prediction_;
        std::optional<fin::core::Timestamp> last_tick_ts_;
        StreamStats stats_{};
    };

} // namespace fin::stream

#endif // FIN_STREAM_SYMBOL_PIPELINE_HPP
