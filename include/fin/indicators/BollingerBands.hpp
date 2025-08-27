#pragma once
#ifndef FIN_INDICATORS_BBANDS_HPP
#define FIN_INDICATORS_BBANDS_HPP

#include <cstddef>
#include <deque>
#include <optional>
#include <vector>
#include <cmath>

namespace fin::indicators
{
    /**
     * @brief Bollinger Bands
     *
     * Middle = SMA(period)
     * Upper = SMA + k * stddev
     * Lower = SMA - k * stddev
     *
     * - Streaming API: update(x) -> optional<Bands>
     * - Batch API: compute(vector<double>, period, k) -> vector<optional<Bands>>
     * - Warmup: returns nullopt until enough samples
     *
     */

    class BollingerBands
    {
    public:
        struct Bands
        {
            double middle;
            double upper;
            double lower;
        };

        BollingerBands(std::size_t period = 20, double k = 2.0);

        void reset();

        std::size_t period() const noexcept { return period_; }
        double k() const noexcept { return k_; }

        std::optional<Bands> update(double x);
        std::optional<Bands> value() const { return current_; }

        static std::vector<std::optional<Bands>>
        compute(const std::vector<double> &values, std::size_t period = 20, double k = 2.0);

    private:
        std::size_t period_;
        double k_;
        std::deque<double> buf_;
        double sum_;
        double sumsq_;
        std::optional<Bands> current_;
    };
} // namespace fin::indicators

#endif // FIN_INDICATORS_BRANDS_HPP
