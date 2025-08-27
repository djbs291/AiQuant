#pragma once
#ifndef FIN_INDICATORS_EMA_HPP
#define FIN_INDICATORS_EMA_HPP

#include <cstddef>
#include <optional>
#include <vector>

namespace fin::indicators
{

    /**
     * Exponential Moving Average (EMA)
     *
     * - Seeds with SMA over the first 'period' samples (warmup).
     * - After warmup: ema = alpha * x + (1 - alpha) * ema_prev
     * - alpha = 2 / (period + 1)
     *
     * Streaming API:
     *   update(x) -> optional<double> (nullopt until seeded)
     *
     * Batch API:
     *   compute(values, period) -> vector<optional<double>>
     */
    class EMA
    {
    public:
        explicit EMA(std::size_t period = 14);

        void reset();

        std::size_t period() const noexcept { return period_; }
        double alpha() const noexcept { return alpha_; }

        // Feed a sample; returns EMA after warmup, otherwise nullopt.
        std::optional<double> update(double x);

        // Last EMA value if available.
        std::optional<double> value() const { return ema_; }

        // Batch helper: compute EMA series with same warmup semantics.
        static std::vector<std::optional<double>>
        compute(const std::vector<double> &values, std::size_t period = 14);

    private:
        std::size_t period_;
        double alpha_;

        // Warmup state for SMA seeding
        std::size_t count_;
        double sum_;
        bool seeded_;

        // Current EMA value after seeding
        std::optional<double> ema_;
    };

} // namespace fin::indicators

#endif // FIN_INDICATORS_EMA_HPP
