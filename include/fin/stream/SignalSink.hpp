#pragma once
#ifndef FIN_STREAM_SIGNAL_SINK_HPP
#define FIN_STREAM_SIGNAL_SINK_HPP

#include <optional>
#include <string_view>

#include "fin/core/Candle.hpp"
#include "fin/indicators/FeatureBus.hpp"
#include "fin/signal/Signal.hpp"

namespace fin::stream
{
    /**
     * @brief One closed candle and what the engine made of it.
     *
     * This is a **view**: every reference and pointer in it belongs to the pipeline and is
     * valid only for the duration of the on_signal() call. A sink that keeps anything must
     * copy it out. A self-contained event would allocate on every bar, which is the one thing
     * the streaming roadmap rules out on the hot path.
     */
    struct StreamEvent
    {
        std::string_view symbol;
        const fin::core::Candle &candle;

        // Null until every selected indicator is warm — warmup is all-or-nothing.
        const fin::indicators::FeatureRow *row = nullptr;

        // The prediction made on the PREVIOUS candle, which is the one this bar was judged
        // on. A candle's own prediction applies to the next bar.
        std::optional<double> prediction;

        const fin::signal::Signal &signal;

        // True only for the partial bar closed by flush() at end of stream.
        bool partial = false;
    };

    class ISignalSink
    {
    public:
        virtual ~ISignalSink() = default;

        // Called on the pipeline's own thread. When per-symbol workers arrive, a dispatcher
        // implements this and pushes onto a queue, so nothing here may assume ordering
        // *across* symbols — only within one.
        virtual void on_signal(const StreamEvent &event) = 0;
    };

} // namespace fin::stream

#endif // FIN_STREAM_SIGNAL_SINK_HPP
