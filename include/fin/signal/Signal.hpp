#pragma once
#ifndef FIN_SIGNAL_SIGNAL_HPP
#define FIN_SIGNAL_SIGNAL_HPP

#include <string>
#include <optional>

#include "fin/core/Timestamp.hpp"

namespace fin::signal
{
    enum class SignalType
    {
        Buy,
        Sell,
        Hold
    };

    struct Signal
    {
        fin::core::Timestamp ts{};
        std::string symbol;    // optional; may be empty if unavailable
        SignalType type = SignalType::Hold;
        double score = 0.0;    // positive -> buy bias; negative -> sell bias
        std::string source;    // short reason/strategy name
    };
}

#endif /* FIN_SIGNAL_SIGNAL_HPP */
