#include "fin/indicators/ZScore.hpp"
#include <algorithm>

namespace fin::indicators
{

    ZScore::ZScore(std::size_t period)
        : period_(period), sum_(0.0), sumsq_(0.0), current_(std::nullopt) {}

    void ZScore::reset()
    {
        buf_.clear();
        sum_ = 0.0;
        sumsq_ = 0.0;
        current_.reset();
    }

    std::optional<double> ZScore::update(double x)
    {
        buf_.push_back(x);
        sum_ += x;
        sumsq_ += x * x;

        if (buf_.size() > period_)
        {
            double old = buf_.front();
            buf_.pop_front();
            sum_ -= old;
            sumsq_ -= old * old;
        }

        if (buf_.size() == period_)
        {
            double n = static_cast<double>(period_);
            double mean = sum_ / n;
            double var = (sumsq_ / n) - mean * mean;
            if (var < 0.0)
                var = 0.0;
            double sd = std::sqrt(var);
            double z = (sd > 0.0) ? (x - mean) / sd : 0.0;
            current_ = z;
            return current_;
        }
        return std::nullopt;
    }

    std::vector<std::optional<double>>
    ZScore::compute(const std::vector<double> &values, std::size_t period)
    {
        std::vector<std::optional<double>> out;
        out.reserve(values.size());
        ZScore zs(period);
        for (double v : values)
            out.emplace_back(zs.update(v));
        return out;
    }

} // namespace fin::indicators
