#include "fin/indicators/VWAP.hpp"
#include <limits>
#include <algorithm>

namespace fin::indicators
{

    VWAP::VWAP() : cum_pv_(0.0), cum_v_(0.0), vwap_(std::nullopt) {}

    // Reset cumulative sums (start of new session/day)
    void VWAP::reset_session()
    {
        cum_pv_ = 0.0;
        cum_v_ = 0.0;
        vwap_ = std::nullopt;
    }

    // Update using a direct trade/price with volume
    double VWAP::update(double price, double volume)
    {
        if (volume < 0.0)
            volume = 0.0; // guard; treat negative volume as 0
        cum_pv_ += price * volume;
        cum_v_ += volume;
        const double denom = (cum_v_ > 0.0) ? cum_v_ : std::numeric_limits<double>::min();
        vwap_ = cum_pv_ / denom;
        return *vwap_;
    }

    // Update using OHLC with volume (uses Typical Price = (H + L + C)/3)
    double VWAP::update(double high, double low, double close, double volume)
    {
        const double tp = (high + low + close) / 3.0;
        return update(tp, volume);
    }

    // Last VWAP value (if any updates happened)
    std::optional<double> VWAP::value() const { return vwap_; }

    // Batch helpers (direct price/volume)
    std::vector<double>
    VWAP::compute(const std::vector<double> &prices,
                  const std::vector<double> &volumes)
    {
        std::size_t n = std::min(prices.size(), volumes.size());
        std::vector<double> out;
        out.reserve(n);
        VWAP v;
        for (std::size_t i = 0; i < n; ++i)
        {
            out.emplace_back(v.update(prices[i], volumes[i]));
        }
        return out;
    }

    std::vector<double> VWAP::compute(const std::vector<double> &highs,
                                      const std::vector<double> &lows,
                                      const std::vector<double> &closes,
                                      const std::vector<double> &volumes)
    {
        std::size_t n = std::min({highs.size(), lows.size(), closes.size(), volumes.size()});
        std::vector<double> out;
        out.reserve(n);
        VWAP v;
        for (std::size_t i = 0; i < n; ++i)
        {
            out.emplace_back(v.update(highs[i], lows[i], closes[i], volumes[i]));
        }
        return out;
    }
} // namespace fin::indicators