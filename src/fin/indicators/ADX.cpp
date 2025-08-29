#include "fin/indicators/ADX.hpp"
#include <algorithm>
#include <cmath>

namespace fin::indicators
{
    static inline double tr_wilder(double h, double l, double prevClose)
    {
        double hl = std::max(0.0, h - l);
        double hc = std::fabs(h - prevClose);
        double lc = std::fabs(l - prevClose);
        return std::max({hl, hc, lc});
    }

    static inline void dm_components(double h, double l, double prevHigh, double prevLow,
                                     double &pdm, double &ndm)
    {
        double upMove = h - prevHigh;
        double downMove = prevLow - l;
        pdm = (upMove > downMove && upMove > 0.0) ? upMove : 0.0;
        ndm = (downMove > upMove && downMove > 0.0) ? downMove : 0.0;
    }

    ADX::ADX(std::size_t period)
        : period_(period)
    {
        reset();
    }

    void ADX::reset()
    {
        have_prev_ = false;
        prev_high_ = prev_low_ = prev_close_ = std::numeric_limits<double>::quiet_NaN();
        seed_count_ = 0;
        tr_sum_ = pdm_sum_ = ndm_sum_ = 0.0;

        di_ready_ = false;
        atr_ = pdm_s_ = ndm_s_ = 0.0;

        dx_seed_count_ = 0;
        dx_sum_ = 0.0;
        adx_ready_ = false;
        adx_ = 0.0;
    }

    std::optional<ADXOut> ADX::update(double high, double low, double close)
    {
        if (!have_prev_)
        {
            prev_high_ = high;
            prev_low_ = low;
            prev_close_ = close;
            have_prev_ = true;
            return std::nullopt;
        }

        // Compute TR, +DM, -DM for this bar
        double tr = tr_wilder(high, low, prev_close_);
        double pdm = 0.0, ndm = 0.0;
        dm_components(high, low, prev_high_, prev_low_, pdm, ndm);

        // Advance prevs for next bar
        prev_high_ = high;
        prev_low_ = low;
        prev_close_ = close;

        // --- Seeding ATR / ADM over first 'period_' observations
        if (!di_ready_)
        {
            tr_sum_ += tr;
            pdm_sum_ += pdm;
            ndm_sum_ += ndm;
            ++seed_count_;

            if (seed_count_ < period_)
            {
                return std::nullopt; // still seeding ATR and DM sums
            }

            // seed_count_ == period_: initialize Wilder-smoothed values
            atr_ = tr_sum_ / static_cast<double>(period_);
            pdm_s_ = pdm_sum_ / static_cast<double>(period_);
            ndm_s_ = ndm_sum_ / static_cast<double>(period_);
            di_ready_ = true;
            // fall-through to compute first DI/DX for this same bar
        }
        else
        {
            // Wilder smoothing updates
            const double N = static_cast<double>(period_);
            atr_ = (atr_ * (N - 1.0) + tr) / N;
            pdm_s_ = (pdm_s_ * (N - 1.0) + pdm) / N;
            ndm_s_ = (ndm_s_ * (N - 1.0) + ndm) / N;
        }

        // Compute DI and DX for this bar (now DI is ready)
        double plusDI = (atr_ > 0.0) ? (100.0 * (pdm_s_ / atr_)) : 0.0;
        double minusDI = (atr_ > 0.0) ? (100.0 * (ndm_s_ / atr_)) : 0.0;

        double denom = plusDI + minusDI;
        double dx = (denom > 0.0) ? (100.0 * std::fabs(plusDI - minusDI) / denom) : 0.0;

        // --- ADX seeding: average of first period_ DX values
        if (!adx_ready_)
        {
            dx_sum_ += dx;
            ++dx_seed_count_;
            if (dx_seed_count_ < period_)
            {
                return std::nullopt; // collecting DX seeds
            }
            // dx_seed_count_ == period_: seed ADX as average of first N DXs
            adx_ = dx_sum_ / static_cast<double>(period_);
            adx_ready_ = true;

            return ADXOut{plusDI, minusDI, dx, adx_};
        }

        // --- After seeding, Wilder-smooth ADX
        const double N = static_cast<double>(period_);
        adx_ = (adx_ * (N - 1.0) + dx) / N;
        return ADXOut{plusDI, minusDI, dx, adx_};
    }

    std::vector<std::optional<ADXOut>>
    ADX::compute(const std::vector<double> &highs,
                 const std::vector<double> &lows,
                 const std::vector<double> &closes,
                 std::size_t period)
    {
        std::size_t n = std::min({highs.size(), lows.size(), closes.size()});
        std::vector<std::optional<ADXOut>> out;
        out.reserve(n);
        ADX adx(period);
        for (std::size_t i = 0; i < n; ++i)
        {
            out.emplace_back(adx.update(highs[i], lows[i], closes[i]));
        }
        return out;
    }
} // namespace fin::indicators
