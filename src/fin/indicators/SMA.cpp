#include "fin/indicators/SMA.hpp"

namespace fin::indicators
{
    SMA::SMA(std::size_t period)
        : period_(period), sum_(0.0), current_(std::nullopt) {}

    void SMA::reset()
    {
        buf_.clear();
        sum_ = 0.0;
        current_.reset();
    }

    std::optional<double> SMA::update(double x)
    {
        buf_.push_back(x);
        sum_ += x;

        if (buf_.size() > period_)
        {
            sum_ -= buf_.front();
            buf_.pop_front();
        }

        if (buf_.size() == period_)
        {
            current_ = sum_ / static_cast<double>(period_);
            return current_;
        }

        return std::nullopt;
    }

    std::vector<std::optional<double>>
    SMA::compute(const std::vector<double> &values, std::size_t period)
    {
        std::vector<std::optional<double>> out;
        out.reserve(values.size());
        SMA sma(period);
        for (double v : values)
        {
            out.emplace_back(sma.update(v));
        }
        return out;
    }

} // namespace fin::indicators
