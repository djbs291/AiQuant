#pragma once
#ifndef FIN_INDICATORS_ATR_HPP
#define FIN_INDICATORS_ATR_HPP

#include <cstddef>
#include <optional>
#include <vector>
#include <cmath>
#include <limits>

namespace fin::indicators
{
    /**
     * Average True Range (ATR) with Wilder's smoothing
     *
     * True Range (TR):
     *  TR_t = max(high - low, abs(high - prevClose), abs(low - prevClose) )
     *
     * Seeding:
     *  - Accumulate `period` values; first ATR is their simple average.
     * Updating (Wilder):
     *  ATR_t = (ATR_{t-1} * (period - 1) + TR_t) / period
     */
    class ATR
    {
    public:
        explicit ATR(std::size_t period = 14);

        void reset();

        std::size_t period() const noexcept { return period_; }

        // Feed one OHLC bar; returns ATR after warmup, otherwise nullopt.
        std::optional<double> update(double high, double low, double close);

        // Last ATR, if available
        std::optional<double> value() const { return atr_; }

        // Batch helper with same warmup semantics.
        static std::vector<std::optional<double>>
        compute(const std::vector<double> &highs,
                const std::vector<double> &lows,
                const std::vector<double> &closes,
                std::size_t period = 14);

    private:
        std::size_t period_;
        bool have_prev_close_ = false;
        double prev_close_ = std::numeric_limits<double>::quiet_NaN();

        // Warmup
        std::size_t tr_count_ = 0;
        double tr_sum_ = 0.0;

        // Runnig ATR after seeding
        std::optional<double> atr_;
    };
} // namespace fin::indicators

#endif // FIN_INDICATORS_ATR_HPP
