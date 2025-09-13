#pragma once
#ifndef FIN_SIGNAL_INDICATORS_SNAPSHOT_HPP
#define FIN_SIGNAL_INDICATORS_SNAPSHOT_HPP

#include <optional>
#include <string>
#include "fin/core/Timestamp.hpp"

namespace fin::signal
{
    struct IndicatorsSnapshot
    {
        fin::core::Timestamp ts{};
        std::string symbol;        // maybe empty if unknown
        double close = 0.0;        // last price/close used
        std::optional<double> rsi; // 0...100
        std::optional<double> ema_fast;
        std::optional<double> ema_slow;
    };
}

#endif /* FIN_SIGNAL_INDICATORS_SNAPSHOT_HPP */
