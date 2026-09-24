#pragma once
#ifndef FIN_STREAM_STREAM_ENGINE_HPP
#define FIN_STREAM_STREAM_ENGINE_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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
     * belongs to, so no stage had to change when routing went from one pipeline to one per
     * symbol.
     *
     * Reject and Skip bind the stream to one symbol and keep exactly one pipeline. Route binds
     * to none: each symbol gets its own pipeline the first time it appears, with its own
     * candles, indicators, warmup and pending prediction, so two instruments in one feed never
     * share a bar or a signal. Every pipeline runs on the calling thread, in tick order; the
     * model and the sink are shared between them.
     */
    class StreamEngine
    {
    public:
        // Throws std::invalid_argument for Route with a config symbol, which asks for one
        // symbol and for all of them at once.
        explicit StreamEngine(StreamConfig config,
                              std::shared_ptr<fin::ml::IModel> model = nullptr,
                              ISignalSink *sink = nullptr);

        // Pinned in place: last_ points into pipelines_, and a copy or a moved-from engine
        // would be left holding a pointer into a map it does not own.
        StreamEngine(const StreamEngine &) = delete;
        StreamEngine &operator=(const StreamEngine &) = delete;
        StreamEngine(StreamEngine &&) = delete;
        StreamEngine &operator=(StreamEngine &&) = delete;

        // The primitive. A push feed or a dequeue loop needs nothing beyond this.
        void on_tick(const fin::core::Tick &tick);

        // End of stream or session: closes every partial candle through the full path, one
        // pipeline at a time in the order the symbols first appeared.
        void flush();

        // Convenience driver for a pull source: drain it, then flush.
        StreamStats run(fin::io::ITickSource &source);

        // Empty until the first tick binds the stream, and always empty under Route.
        [[nodiscard]] const std::string &bound_symbol() const noexcept { return bound_symbol_; }

        // Every symbol that has a pipeline, in order of first appearance: one entry at most
        // under Reject and Skip, one per instrument seen under Route.
        [[nodiscard]] const std::vector<std::string> &symbols() const noexcept { return order_; }

        // By value: the routing counters live here and the stage counters live in the
        // pipelines, and the caller wants one view of both. Under Route the pipelines' counters
        // are summed. Not on the hot path.
        [[nodiscard]] StreamStats stats() const;

        // One pipeline's counters, in the order of symbols(). Routing counters are not
        // included: a dropped tick belongs to no pipeline.
        [[nodiscard]] std::vector<std::pair<std::string, StreamStats>> stats_by_symbol() const;

    private:
        SymbolPipeline &pipeline_for(const std::string &symbol);

        StreamConfig config_;
        std::shared_ptr<fin::ml::IModel> model_;
        ISignalSink *sink_ = nullptr;

        std::string bound_symbol_;
        // Keyed on std::string rather than fin::core::Symbol, which has only operator== and
        // would need a hash first. unordered_map never moves its elements, so a pipeline's
        // address is stable while others are added.
        std::unordered_map<std::string, SymbolPipeline> pipelines_;
        // The map's iteration order is unspecified; this is the one flush and stats follow,
        // so the same file always produces the same output in the same order.
        std::vector<std::string> order_;
        // The pipeline the previous tick went to. Safe to keep: see the map above.
        SymbolPipeline *last_ = nullptr;

        std::size_t foreign_ticks_ = 0;
    };

} // namespace fin::stream

#endif // FIN_STREAM_STREAM_ENGINE_HPP
