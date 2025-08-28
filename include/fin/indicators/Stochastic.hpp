#pragma once
#ifndef FIN_INDICATORS_STOCHASTIC_HPP
#define FIN_INDICATORS_STOCHASTIC_HPP

#include <cstddef>
#include <deque>
#include <optional>
#include <vector>
#include <cmath>

namespace fin::indicators
{
    struct StochOut
    {
        double k; // %K
        double d; // %D (SMA of %K)
    };

    /**
     * @brief Stochastic Oscillator (%K, %D)
     * - Rolling HH/LL in O(1) via deques monotónicas
     * - %K = 100 * (close - LL) / (HH - LL); if HH==LL => %K = 50
     * - %D = SMA(%K, dPeriod).
     * - update(high, low, close) returns std::nullopt until both (K and D) having warmup
     */

    class Stochastic
    {
    public:
        Stochastic(std::size_t kPeriod = 14, std::size_t dPeriod = 3);

        void reset();

        std::size_t k_period() const noexcept { return kPeriod_; }
        std::size_t d_period() const noexcept { return dPeriod_; }

        // One OHLC bar per tick
        std::optional<StochOut> update(double high, double low, double close);

        // Last Value, if exists
        std::optional<StochOut> value() const { return current_; }

        // Batch helper (same semantics for warmup)
        static std::vector<std::optional<StochOut>>
        compute(const std::vector<double> &highs,
                const std::vector<double> &lows,
                const std::vector<double> &closes,
                std::size_t kPeriod = 14, std::size_t dPeriod = 3);

    private:
        struct Node
        {
            double v;
            std::size_t idx;
        };

        // Keep the maximum (descending) and the minimum (increasing) in O(1)
        void push_high(double h);
        void push_low(double l);
        void evict_old(std::size_t window_start);

        double highest_high() const { return dqHigh_.empty() ? NAN : dqHigh_.front().v; }
        double lowest_low() const { return dqLow_.empty() ? NAN : dqLow_.front().v; }

        std::size_t kPeriod_;
        std::size_t dPeriod_;

        // Monothonics Deques to HH/LL
        std::deque<Node> dqHigh_; // descending values (front = greatest)
        std::deque<Node> dqLow_;  // ascending values (front = minor)

        // Current Index (0-based)
        std::size_t idx_ = 0;

        // For %D (SMA of %k)
        std::deque<double> kBuf_;
        double kSum_ = 0.0;

        std::optional<StochOut> current_;
    };
} // namespace fin::indicators

#endif // FIN_INDICATORS_STOCHASTIC_HPP
