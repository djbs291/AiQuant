#pragma once
#ifndef FIN_INDICATORS_MACD_HPP
#define FIN_INDICATORS_MACD_HPP

#include <cstddef>
#include <optional>
#include <vector>
#include "fin/indicators/EMA.hpp"

namespace fin::indicators
{

    struct MACDValue
    {
        double macd;   // EMA_fast - EMA_slow
        double signal; // EMA(signal_period) over MACD
        double hist;   // macd - signal
    };

    class MACD
    {
    public:
        MACD(std::size_t fast = 12, std::size_t slow = 26, std::size_t signal = 9);

        void reset();

        // Returns value only when signal EMA is available (after full warmup)
        std::optional<MACDValue> update(double close);

        static std::vector<std::optional<MACDValue>>
        compute(const std::vector<double> &closes,
                std::size_t fast = 12, std::size_t slow = 26, std::size_t signal = 9);

        std::size_t fast_period() const noexcept { return fast_; }
        std::size_t slow_period() const noexcept { return slow_; }
        std::size_t signal_period() const noexcept { return signal_; }

    private:
        std::size_t fast_;
        std::size_t slow_;
        std::size_t signal_;

        EMA ema_fast_;
        EMA ema_slow_;
        EMA ema_signal_;

        std::optional<MACDValue> current_;
    };

} // namespace fin::indicators

#endif // FIN_INDICATORS_MACD_HPP
