#include "fin/indicators/EMA.hpp"

namespace fin::indicators
{

    EMA::EMA(std::size_t period)
        : period_(period),
          alpha_(2.0 / (static_cast<double>(period) + 1.0)),
          count_(0),
          sum_(0.0),
          seeded_(false),
          ema_(std::nullopt)
    {
    }

    void EMA::reset()
    {
        count_ = 0;
        sum_ = 0.0;
        seeded_ = false;
        ema_.reset();
    }

    std::optional<double> EMA::update(double x)
    {
        if (!seeded_)
        {
            sum_ += x;
            ++count_;
            if (count_ == period_)
            {
                ema_ = sum_ / static_cast<double>(period_); // seed with SMA
                seeded_ = true;
                return ema_;
            }
            return std::nullopt;
        }

        // Exponential smoothing
        double v = alpha_ * x + (1.0 - alpha_) * *ema_;
        ema_ = v;
        return ema_;
    }

    std::vector<std::optional<double>>
    EMA::compute(const std::vector<double> &values, std::size_t period)
    {
        std::vector<std::optional<double>> out;
        out.reserve(values.size());
        EMA ema(period);
        for (double x : values)
            out.emplace_back(ema.update(x));
        return out;
    }

} // namespace fin::indicators
