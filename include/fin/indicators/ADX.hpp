#pragma once
#ifndef FIN_INDICATORS_ADX_HPP
#define FIN_INDICATORS_ADX_HPP

#include <cstddef>
#include <optional>
#include <vector>
#include <limits>

namespace fin::indicators
{
    struct ADXOut
    {
        /* data */
        double plusDI;  // +DI
        double minusDI; // -DI
        double dx;      // DX of current bar
        double adx;     // ADX (only valid after full ADX warmup)
    };

    class ADX
    {
    public:
        explicit ADX(std::size_t period = 14);

        void reset();

        std::size_t period() const noexcept { return period_; }

        // Feed on OHLC bar; returns ADXOut only when ADX is available (after warmup)
        std::optional<ADXOut> update(double high, double low, double close);

        // Batch helper with identical warmup semantics
        static std::vector<std::optional<ADXOut>>
        compute(const std::vector<double> &highs,
                const std::vector<double> &lows,
                const std::vector<double> &closes,
                std::size_t period = 14);

    private:
        std::size_t period_;

        // Previous bar
        bool have_prev_ = false;
        double prev_high_ = std::numeric_limits<double>::quiet_NaN();
        double prev_low_ = std::numeric_limits<double>::quiet_NaN();
        double prev_close_ = std::numeric_limits<double>::quiet_NaN();

        // Seeding accumulators for ATR, +DM, -DM
        std::size_t seed_count_ = 0;
        double tr_sum_ = 0.0;
        double pdm_sum_ = 0.0;
        double ndm_sum_ = 0.0;

        // Wilder-smoothed running values after seeding
        bool di_ready_ = false; // after initial ATR/pDM/nDM are set
        double atr_ = 0.0;
        double pdm_s_ = 0.0;
        double ndm_s_ = 0.0;

        // ADX seeding (collect first N DX)
        std::size_t dx_seed_count_ = 0;
        std::size_t dx_sum_ = 0.0;
        bool adx_ready_ = false;
        double adx_ = 0.0;
    };

} // namespace fin::indicators

#endif // FIN_INDICATORS_ADX_HPP
