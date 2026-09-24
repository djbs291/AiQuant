#include "fin/stream/StreamEngine.hpp"

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace fin::stream
{
    namespace
    {
        // Field by field, so a counter added to StreamStats has to be added here too, or the
        // routed totals would silently leave it at zero. The assert turns forgetting into a
        // compile error: update the count once the new field is summed below.
        static_assert(sizeof(StreamStats) == 11 * sizeof(std::size_t),
                      "StreamStats changed: add the new counter to accumulate()");

        void accumulate(StreamStats &total, const StreamStats &part)
        {
            total.ticks += part.ticks;
            total.ticks_out_of_order += part.ticks_out_of_order;
            total.ticks_other_symbol += part.ticks_other_symbol;
            total.candles += part.candles;
            total.feature_rows += part.feature_rows;
            total.predictions += part.predictions;
            total.prediction_errors += part.prediction_errors;
            total.signals += part.signals;
            total.buys += part.buys;
            total.sells += part.sells;
            total.holds += part.holds;
        }
    } // namespace

    StreamEngine::StreamEngine(StreamConfig config,
                               std::shared_ptr<fin::ml::IModel> model,
                               ISignalSink *sink)
        : config_(std::move(config)), model_(std::move(model)), sink_(sink)
    {
        if (config_.foreign_symbol == SymbolPolicy::Route && !config_.symbol.empty())
        {
            throw std::invalid_argument("stream symbol '" + config_.symbol +
                                        "' names one instrument, but routing takes every one; "
                                        "set either the symbol or the Route policy");
        }
    }

    SymbolPipeline &StreamEngine::pipeline_for(const std::string &symbol)
    {
        // Ticks for one instrument usually arrive in runs, and a single-symbol stream is one
        // long run, so a string compare saves hashing the symbol on almost every tick.
        if (last_ && last_->symbol() == symbol)
            return *last_;

        auto it = pipelines_.find(symbol);
        if (it == pipelines_.end())
        {
            it = pipelines_.try_emplace(symbol, symbol, config_, model_, sink_).first;
            order_.push_back(symbol);
        }
        last_ = &it->second;
        return *last_;
    }

    void StreamEngine::on_tick(const fin::core::Tick &tick)
    {
        const std::string &symbol = tick.symbol().value();

        if (config_.foreign_symbol == SymbolPolicy::Route)
        {
            pipeline_for(symbol).on_tick(tick);
            return;
        }

        if (pipelines_.empty())
        {
            // An explicit config symbol wins; otherwise the first tick binds the stream.
            bound_symbol_ = config_.symbol.empty() ? symbol : config_.symbol;
            pipeline_for(bound_symbol_);
        }

        if (symbol != bound_symbol_)
        {
            if (config_.foreign_symbol == SymbolPolicy::Reject)
            {
                // Refusing beats blending: one candle series built from two instruments opens
                // on one and closes on the other, and measures something that does not exist.
                throw std::invalid_argument(
                    "stream is bound to symbol '" + bound_symbol_ + "' but saw '" + symbol +
                    "'; name one with --symbol, or route each to its own pipeline with --per-symbol");
            }

            ++foreign_ticks_;
            return;
        }

        pipeline_for(bound_symbol_).on_tick(tick);
    }

    void StreamEngine::flush()
    {
        for (const auto &symbol : order_)
            pipelines_.at(symbol).flush();
    }

    StreamStats StreamEngine::run(fin::io::ITickSource &source)
    {
        while (auto tick = source.next())
            on_tick(*tick);
        flush();
        return stats();
    }

    StreamStats StreamEngine::stats() const
    {
        StreamStats merged{};
        for (const auto &symbol : order_)
            accumulate(merged, pipelines_.at(symbol).stats());

        // Ticks dropped by routing never reached a pipeline, so they are counted here and
        // added back into the total the caller sees.
        merged.ticks += foreign_ticks_;
        merged.ticks_other_symbol = foreign_ticks_;
        return merged;
    }

    std::vector<std::pair<std::string, StreamStats>> StreamEngine::stats_by_symbol() const
    {
        std::vector<std::pair<std::string, StreamStats>> out;
        out.reserve(order_.size());
        for (const auto &symbol : order_)
            out.emplace_back(symbol, pipelines_.at(symbol).stats());
        return out;
    }

} // namespace fin::stream
