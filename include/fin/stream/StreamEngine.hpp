#pragma once
#ifndef FIN_STREAM_STREAM_ENGINE_HPP
#define FIN_STREAM_STREAM_ENGINE_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include "fin/core/Tick.hpp"
#include "fin/io/Sources.hpp"
#include "fin/ml/IModel.hpp"
#include "fin/stream/SignalSink.hpp"
#include "fin/stream/StreamConfig.hpp"
#include "fin/stream/SymbolPipeline.hpp"

namespace fin::stream
{
    /**
     * @brief Routing and symbol policy. The stages live in SymbolPipeline.
     *
     * The split is the point: this class is the only thing that decides which state a tick
     * belongs to, so the per-symbol-worker PR replaces one member — the optional below
     * becomes a map — and a queue fan-out replaces this method, without touching a stage.
     *
     * Single-symbol by design for now. A file carrying a second symbol is refused rather than
     * blended into one candle series, which is what the batch path still does silently.
     */
    class StreamEngine
    {
    public:
        explicit StreamEngine(StreamConfig config,
                              std::shared_ptr<fin::ml::IModel> model = nullptr,
                              ISignalSink *sink = nullptr);

        // The primitive. A push feed or a dequeue loop needs nothing beyond this.
        void on_tick(const fin::core::Tick &tick);

        // End of stream or session: closes the partial candle through the full path.
        void flush();

        // Convenience driver for a pull source: drain it, then flush.
        StreamStats run(fin::io::ITickSource &source);

        // Empty until the first tick binds the stream.
        [[nodiscard]] const std::string &bound_symbol() const noexcept { return bound_symbol_; }

        // By value: the routing counters live here and the stage counters live in the
        // pipeline, and the caller wants one view of both. Not on the hot path.
        [[nodiscard]] StreamStats stats() const;

    private:
        StreamConfig config_;
        std::shared_ptr<fin::ml::IModel> model_;
        ISignalSink *sink_ = nullptr;

        std::string bound_symbol_;
        // Exactly one for now. This is the member the multi-symbol router replaces with an
        // unordered_map keyed on std::string — on string, because fin::core::Symbol has only
        // operator== and would need an ordering or a hash first.
        std::optional<SymbolPipeline> pipeline_;

        std::size_t foreign_ticks_ = 0;
    };

} // namespace fin::stream

#endif // FIN_STREAM_STREAM_ENGINE_HPP
