#pragma once
#ifndef FIN_INDICATORS_ZSCORE_HPP
#define FIN_INDICATORS_ZSCORE_HPP

#include <cstddef>
#include <deque>
#include <optional>
#include <vector>
#include <cmath>

namespace fin::indicators
{

    /**
     * Rolling Z-Score of price over a fixed window.
     * z = (x - mean) / stddev, with mean/stddev over the last `period` samples.
     * Warmup: returns nullopt until `period` samples are available.
     * If stddev == 0, returns 0 (neutral).
     */
    class ZScore
    {
    public:
        explicit ZScore(std::size_t period = 20);

        void reset();

        std::size_t period() const noexcept { return period_; }

        // Streaming update with latest price.
        std::optional<double> update(double x);

        // Last computed z, if any.
        std::optional<double> value() const { return current_; }

        // Batch convenience with same warmup semantics.
        static std::vector<std::optional<double>>
        compute(const std::vector<double> &values, std::size_t period = 20);

    private:
        std::size_t period_;
        std::deque<double> buf_;
        double sum_;
        double sumsq_;
        std::optional<double> current_;
    };

} // namespace fin::indicators

#endif // FIN_INDICATORS_ZSCORE_HPP
