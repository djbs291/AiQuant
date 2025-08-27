#include "fin/indicators/BollingerBands.hpp"

namespace fin::indicators
{

    BollingerBands::BollingerBands(std::size_t period, double k)
        : period_(period), k_(k), sum_(0.0), sumsq_(0.0), current_(std::nullopt) {}

    void BollingerBands::reset()
    {
        buf_.clear();
        sum_ = 0.0;
        sumsq_ = 0.0;
        current_.reset();
    }

    std::optional<BollingerBands::Bands> BollingerBands::update(double x)
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
            double mean = sum_ / static_cast<double>(period_);
            double var = (sumsq_ / static_cast<double>(period_)) - (mean * mean);
            if (var < 0.0)
                var = 0.0; // numerical safety
            double stdev = std::sqrt(var);

            current_ = Bands{mean, mean + k_ * stdev, mean - k_ * stdev};
            return current_;
        }

        return std::nullopt;
    }

    std::vector<std::optional<BollingerBands::Bands>>
    BollingerBands::compute(const std::vector<double> &values, std::size_t period, double k)
    {
        std::vector<std::optional<Bands>> out;
        out.reserve(values.size());
        BollingerBands bb(period, k);
        for (double v : values)
            out.emplace_back(bb.update(v));
        return out;
    }

} // namespace fin::indicators
