#include "fin/stream/StreamEngine.hpp"

#include <stdexcept>
#include <utility>

namespace fin::stream
{
    StreamEngine::StreamEngine(StreamConfig config,
                               std::shared_ptr<fin::ml::IModel> model,
                               ISignalSink *sink)
        : config_(std::move(config)), model_(std::move(model)), sink_(sink)
    {
    }

    void StreamEngine::on_tick(const fin::core::Tick &tick)
    {
        const std::string &symbol = tick.symbol().value();

        if (!pipeline_)
        {
            // An explicit config symbol wins; otherwise the first tick binds the stream.
            bound_symbol_ = config_.symbol.empty() ? symbol : config_.symbol;
            pipeline_.emplace(bound_symbol_, config_, model_, sink_);
        }

        if (symbol != bound_symbol_)
        {
            if (config_.foreign_symbol == SymbolPolicy::Reject)
            {
                // Refusing beats blending. The batch path merges every symbol in a file into
                // one candle series without a word, and that is a bug, not a feature.
                throw std::invalid_argument(
                    "stream is bound to symbol '" + bound_symbol_ + "' but saw '" + symbol +
                    "'; this single-symbol pipeline refuses to blend them (name one with --symbol)");
            }

            ++foreign_ticks_;
            return;
        }

        pipeline_->on_tick(tick);
    }

    void StreamEngine::flush()
    {
        if (pipeline_)
            pipeline_->flush();
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
        StreamStats merged = pipeline_ ? pipeline_->stats() : StreamStats{};
        // Ticks dropped by routing never reached the pipeline, so they are counted here and
        // added back into the total the caller sees.
        merged.ticks += foreign_ticks_;
        merged.ticks_other_symbol = foreign_ticks_;
        return merged;
    }

} // namespace fin::stream
