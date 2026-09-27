#pragma once
#ifndef FIN_STREAM_STREAM_ENGINE_HPP
#define FIN_STREAM_STREAM_ENGINE_HPP

#include <cstddef>
#include <functional>
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
#include "fin/stream/SymbolModel.hpp"
#include "fin/stream/SymbolPipeline.hpp"

namespace fin::stream
{
    // Called once per symbol, when its pipeline is created. Returning a null model is allowed:
    // that symbol runs without predictions. A struct rather than a bare std::function alias
    // on purpose: `StreamEngine(cfg, nullptr, &sink)` would otherwise match both constructors.
    struct ModelResolver
    {
        std::function<SymbolModel(const std::string &symbol)> resolve;
    };

    /**
     * @brief Routing and symbol policy. The stages live in SymbolPipeline.
     *
     * The split is the point: this class is the only thing that decides which state a tick
     * belongs to, so no stage had to change when routing went from one pipeline to one per
     * symbol.
     *
     * Reject and Skip bind the stream to one symbol and keep exactly one pipeline. Route binds
     * to none: each symbol gets its own pipeline the first time it appears, with its own
     * candles, indicators, warmup and out-of-order clock, so two instruments in one feed never
     * share a bar or a signal. Every pipeline runs on the calling thread, in tick order, and
     * the sink is shared between them.
     *
     * The model is chosen per pipeline by a ModelResolver, and so are the feature set, the
     * indicator periods and the timeframe: each pipeline builds its candles and features the
     * way its own model was trained. That matters
     * because LinearModel::predict skips names it does not know, so one feature set for models
     * trained on different ones would score some of them on a subset of their weights, silently.
     */
    class StreamEngine
    {
    public:
        // Throws std::invalid_argument for Route with a config symbol, which asks for one
        // symbol and for all of them at once.
        // One model for every symbol, its features taken from config.features.
        explicit StreamEngine(StreamConfig config,
                              std::shared_ptr<fin::ml::IModel> model = nullptr,
                              ISignalSink *sink = nullptr);

        // A model per symbol. Creating a pipeline throws std::invalid_argument when the
        // resolved model records a feature set and config.features names a different one. The
        // resolved params and timeframe replace config's for that pipeline: whether a config
        // value was chosen or defaulted is not known here, so contradictions between what the
        // user typed and what a model file records are caught earlier, by symbol_model_from.
        StreamEngine(StreamConfig config, ModelResolver resolver, ISignalSink *sink = nullptr);

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

        // Whether that symbol's pipeline predicts; false for a symbol with no pipeline.
        [[nodiscard]] bool has_model(const std::string &symbol) const;

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
        ModelResolver resolver_;
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
