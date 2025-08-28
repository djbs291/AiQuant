#include "fin/indicators/ATR.hpp"
#include <algorithm>

namespace fin::indicators
{
    ATR::ATR(std::size_t period) : period_(period)
    {
        reset();
    }

    void ATR::reset()
    {
        have_prev_close_ = false;
        prev_close_ = std::numeric_limits<double>::quiet_NaN();
        tr_count_ = 0;
        tr_sum_ = 0.0;
        atr_.reset();
    }

    std::optional<double> ATR::update(double high, double low, double close)
    {
        // On the very first bar we don't have prevClose; we can't compute TR yet.
        if (!have_prev_close_)
        {
            prev_close_ = close;
            have_prev_close_ = true;
            return std::nullopt;
        }

        // Compute True Range (TR)
        double hl = std::max(0.0, high - low);
        double hc = std::fabs(high - prev_close_);
        double lc = std::fabs(low - prev_close_);
        double tr = std::max({hl, hc, lc});

        // Advance prev_close for next bar
        prev_close_ = close;

        // Warmup accumulation
        if (!atr_.has_value())
        {
            tr_sum_ += tr;
            ++tr_count_;
            if (tr_count_ == period_)
            {
                atr_ = tr_sum_ / static_cast<double>(period_);
                return atr_;
            }
            return std::nullopt;
        }

        // Wilder smoothing after seeding
        double new_atr = ((*atr_) * (static_cast<double>(period_) - 1.0) + tr) / static_cast<double>(period_);
        atr_ = new_atr;
        return atr_;
    }

    std::vector<std::optional<double>>
    ATR::compute(const std::vector<double> &highs,
                 const std::vector<double> &lows,
                 const std::vector<double> &closes,
                 std::size_t period)
    {
        std::size_t n = std::min({highs.size(), lows.size(), closes.size()});
        std::vector<std::optional<double>> out;
        out.reserve(n);
        ATR atr(period);
        for (std::size_t i = 0; i < n; ++i)
        {
            out.emplace_back(atr.update(highs[i], lows[i], closes[i]));
        }
        return out;
    }
} // namespace fin::indicators
