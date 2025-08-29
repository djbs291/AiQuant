#pragma once
#ifndef FIN_INDICATORS_VWAP_HPP
#define FIN_INDICATORS_VWAP_HPP

#include <cstddef>
#include <optional>
#include <vector>
#include <cmath>
#include <limits>

namespace fin::indicators
{
    /**
     * Session-scoped VWAP:
     *  cumPV = Σ(price * volume)
     *  cumV  = Σ(volume)
     *  vwap  = cumPV / cumV    (guarded)
     *
     *  overloads:
     *    - update(price, volume);
     *    - update(high, low, close, volume) -> uses Typical Price (H+L+C)/3
     *
     *  Notes:
     *    - Returns current VWAP as double (available from first update)
     *    - Call reset_session() at your session boundary
     */

    class VWAP
    {
    public:
        VWAP();

        // Reset cumulative sums (start of new session/day)
        void reset_session();

        // Update using a direct trade/price with volume
        double update(double price, double volume);

        // Update using OHLC with volume (uses Typical Price = (H + L + C)/3)
        double update(double high, double low, double close, double volume);

        // Last VWAP value (if any updates happened)
        std::optional<double> value() const;

        // Batch helpers (direct price/volume)
        static std::vector<double>
        compute(const std::vector<double> &prices,
                const std::vector<double> &volumes);

        static std::vector<double>
        compute(const std::vector<double> &highs,
                const std::vector<double> &lows,
                const std::vector<double> &closes,
                const std::vector<double> &volumes);

    private:
        double cum_pv_;
        double cum_v_;
        std::optional<double> vwap_;
    };
} // namespace fin::indicators

#endif // FIN_INDICATORS_VWAP_HPP
