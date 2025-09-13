#pragma once
#ifndef FIN_SIGNAL_ENGINE_HPP
#define FIN_SIGNAL_ENGINE_HPP

#include <optional>
#include <string>

#include "fin/signal/IndicatorsSnapshot.hpp"
#include "fin/signal/Signal.hpp"

namespace fin::signal
{
    struct SignalEngineConfig
    {
        // RSI bounds for overbought/oversold
        double rsi_buy_below = 30.0;
        double rsi_sell_above = 70.0;

        // If both EMAs are present, use crossover bias
        bool use_ema_crossover = true;
    };

    class SignalEngine
    {
    public:
        explicit SignalEngine(SignalEngineConfig cfg = {}) : cfg_(cfg) {}

        Signal eval(const IndicatorsSnapshot &snap, std::optional<double> prediction = std::nullopt) const;

    private:
        SignalEngineConfig cfg_{};
    };
}

#endif /* FIN_SIGNAL_ENGINE_HPP */
