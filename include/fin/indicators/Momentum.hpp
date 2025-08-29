#pragma once
#ifndef FIN_INDICATORS_MOMENTUM_HPP
#define FIN_INDICATORS_MOMENTUM_HPP

#include <cstddef>
#include <deque>
#include <optional>
#include <vector>
#include <cmath>

namespace fin::indicators
{

    /**
     * Momentum over a lookback period.
     * Two common modes:
     *  - Difference: M = close_t - close_{t-period}
     *  - Rate:       M = (close_t / close_{t-period}) - 1
     * Warmup: returns nullopt until we have the price from `period` ago.
     */
    class Momentum
    {
    public:
        enum class Mode
        {
            Difference,
            Rate
        };

        Momentum(std::size_t period = 10, Mode mode = Mode::Difference);

        void reset();

        std::size_t period() const noexcept { return period_; }
        Mode mode() const noexcept { return mode_; }

        std::optional<double> update(double close);

        std::optional<double> value() const { return current_; }

        static std::vector<std::optional<double>>
        compute(const std::vector<double> &closes,
                std::size_t period = 10,
                Mode mode = Mode::Difference);

    private:
        std::size_t period_;
        Mode mode_;
        std::deque<double> buf_;
        std::optional<double> current_;
    };

} // namespace fin::indicators

#endif // FIN_INDICATORS_MOMENTUM_HPP
