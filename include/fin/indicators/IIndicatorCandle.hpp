#pragma once
#ifndef FIN_INDICATORS_IINDICATOR_CANDLE_HPP
#define FIN_INDICATORS_IINDICATOR_CANDLE_HPP

#include "fin/core/Candle.hpp"

namespace fin::indicators
{

    // Scalar indicator that consumes an OHLCV Candle.
    class IIndicatorScalarCandle
    {
    public:
        virtual ~IIndicatorScalarCandle() = default;
        virtual void update(const Candle &c) = 0;
        virtual bool is_ready() const = 0;
        virtual double value() const = 0; // only valid if is_ready()
        virtual void reset() {}           // default no-op
    };

} // namespace fin::indicators

#endif // FIN_INDICATORS_IINDICATOR_CANDLE_HPP
