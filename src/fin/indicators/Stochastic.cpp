#include "fin/indicators/Stochastic.hpp"
#include <algorithm>
#include <limits>

namespace fin::indicators
{

    Stochastic::Stochastic(std::size_t kPeriod, std::size_t dPeriod)
        : kPeriod_(kPeriod), dPeriod_(dPeriod)
    {
        reset();
    }

    void Stochastic::reset()
    {
        dqHigh_.clear();
        dqLow_.clear();
        idx_ = 0;
        kBuf_.clear();
        kSum_ = 0.0;
        current_.reset();
    }

    void Stochastic::push_high(double h)
    {
        while (!dqHigh_.empty() && dqHigh_.back().v <= h)
        {
            dqHigh_.pop_back();
        }
        dqHigh_.push_back(Node{h, idx_});
    }

    void Stochastic::push_low(double l)
    {
        while (!dqLow_.empty() && dqLow_.back().v >= l)
        {
            dqLow_.pop_back();
        }
        dqLow_.push_back(Node{l, idx_});
    }

    void Stochastic::evict_old(std::size_t window_start)
    {
        while (!dqHigh_.empty() && dqHigh_.front().idx < window_start)
            dqHigh_.pop_front();
        while (!dqLow_.empty() && dqLow_.front().idx < window_start)
            dqLow_.pop_front();
    }

    std::optional<StochOut> Stochastic::update(double high, double low, double close)
    {
        // Insert current high/low
        push_high(high);
        push_low(low);

        // Keep only last kPeriod_ elements in window
        if (idx_ + 1 >= kPeriod_)
        {
            std::size_t window_start = idx_ + 1 - kPeriod_;
            evict_old(window_start);
        }

        std::optional<StochOut> out = std::nullopt;

        // Compute %K once we have at least kPeriod_ samples
        if (idx_ + 1 >= kPeriod_)
        {
            double hh = highest_high();
            double ll = lowest_low();

            double k;
            if (hh == ll)
            {
                k = 50.0; // neutral when no range
            }
            else
            {
                k = 100.0 * (close - ll) / (hh - ll);
                if (k < 0.0)
                    k = 0.0;
                if (k > 100.0)
                    k = 100.0;
            }

            // Maintain SMA of K for %D
            kBuf_.push_back(k);
            kSum_ += k;
            if (kBuf_.size() > dPeriod_)
            {
                kSum_ -= kBuf_.front();
                kBuf_.pop_front();
            }

            if (kBuf_.size() == dPeriod_)
            {
                double d = kSum_ / static_cast<double>(dPeriod_);
                current_ = StochOut{k, d};
                out = current_;
            }
        }

        ++idx_;
        return out;
    }

    std::vector<std::optional<StochOut>>
    Stochastic::compute(const std::vector<double> &highs,
                        const std::vector<double> &lows,
                        const std::vector<double> &closes,
                        std::size_t kPeriod, std::size_t dPeriod)
    {
        std::size_t n = std::min({highs.size(), lows.size(), closes.size()});
        std::vector<std::optional<StochOut>> out;
        out.reserve(n);
        Stochastic stoch(kPeriod, dPeriod);
        for (std::size_t i = 0; i < n; ++i)
        {
            out.emplace_back(stoch.update(highs[i], lows[i], closes[i]));
        }
        return out;
    }

} // namespace fin::indicators
