#pragma once
#ifndef FIN_INDICATORS_SMA_HPP
#define FIN_INDICATORS_SMA_CPP

#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

namespace fin::indicators
{

    /**
     * @brief Simple Moving Average (SMA)
     *
     * - Streaming API: update(x) -> optional<double>
     * - Batch API: compute(vector<double>, period) -> vector<optional<double>>
     * - Warmup: returns std::nullopt until enough samples are available
     * - O(1) per update, O(period) space
     */

    class SMA
    {
    public:
        explicit SMA(std::size_t period = 14);

        void reset();
        std::size_t period() const noexcept { return period_; }

        // Feed a sample; returns SMA if window full, otherwise nullopt
        std::optional<double> update(double x);

        // Last computed SMA value (if available)
        std::optional<double> value() const { return current_; }

        // Batch mode: compute SMA series for a full vector
        static std::vector<std::optional<double>>
        compute(const std::vector<double> &values, std::size_t period = 14);

    private:
        std::size_t period_;
        std::deque<double> buf_;
        double sum_;
        std::optional<double> current_;
    };

} // namespace fin::indicators

#endif // FIN_INDICATORS_SMA_CPP
