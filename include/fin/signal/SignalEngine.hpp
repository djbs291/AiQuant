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
        // What the model's vote is worth, against 1 for each indicator rule. At the default
        // 0.5 it can only break a tie between RSI and EMA; above 1 it can overrule one rule,
        // above 2 both. Zero leaves the model out of the decision. Must be finite and >= 0.
        double model_weight = 0.5;
    };

    class SignalEngine
    {
    public:
        explicit SignalEngine(SignalEngineConfig cfg = {}) : cfg_(cfg) {}

        Signal eval(const IndicatorsSnapshot &snap, std::optional<double> prediction = std::nullopt) const;

        [[nodiscard]] const SignalEngineConfig &config() const noexcept { return cfg_; }

    private:
        SignalEngineConfig cfg_{};
    };
}

#endif /* FIN_SIGNAL_ENGINE_HPP */
