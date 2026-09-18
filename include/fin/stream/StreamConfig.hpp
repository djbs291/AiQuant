#pragma once
#ifndef FIN_STREAM_STREAM_CONFIG_HPP
#define FIN_STREAM_STREAM_CONFIG_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "fin/indicators/FeatureSpec.hpp"
#include "fin/io/Options.hpp"
#include "fin/signal/SignalEngine.hpp"

namespace fin::stream
{
    // What to do with a tick whose symbol is not the one the stream is bound to.
    enum class SymbolPolicy
    {
        Reject, // throw: a mixed file is a mistake unless the caller says otherwise
        Skip    // drop and count it: the caller asked for one symbol out of many
    };

    struct StreamConfig
    {
        fin::io::Timeframe timeframe = fin::io::Timeframe::M1;

        // Empty binds the stream to the first tick's symbol.
        std::string symbol;
        SymbolPolicy foreign_symbol = SymbolPolicy::Reject;

        // Empty means indicators::default_feature_names(), exactly as in a scenario.
        std::vector<std::string> features;

        // Feeds the FeatureBus and, through ema_fast/ema_slow/rsi, the snapshot indicators.
        // run_scenario couples those two the same way, which is what lets the streaming and
        // batch paths be compared bar for bar.
        fin::indicators::FeatureParams params{};

        fin::signal::SignalEngineConfig signal{};
    };

    // Plain counters, deliberately without logic: when workers arrive they will be summed
    // across pipelines rather than shared.
    struct StreamStats
    {
        std::size_t ticks = 0;
        std::size_t ticks_out_of_order = 0;
        std::size_t ticks_other_symbol = 0;
        std::size_t candles = 0;
        std::size_t feature_rows = 0;
        std::size_t predictions = 0;
        std::size_t prediction_errors = 0;
        std::size_t signals = 0;
        std::size_t buys = 0;
        std::size_t sells = 0;
        std::size_t holds = 0;
    };

} // namespace fin::stream

#endif // FIN_STREAM_STREAM_CONFIG_HPP
