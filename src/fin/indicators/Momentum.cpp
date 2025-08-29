#include "fin/indicators/Momentum.hpp"

namespace fin::indicators
{

    Momentum::Momentum(std::size_t period, Mode mode)
        : period_(period), mode_(mode), current_(std::nullopt) {}

    void Momentum::reset()
    {
        buf_.clear();
        current_.reset();
    }

    std::optional<double> Momentum::update(double close)
    {
        buf_.push_back(close);
        if (buf_.size() <= period_)
        {
            return std::nullopt; // need price from period bars ago
        }

        double prev = buf_.front();
        buf_.pop_front();

        double m;
        if (mode_ == Mode::Difference)
        {
            m = close - prev;
        }
        else
        {
            // Rate of change
            m = (prev != 0.0) ? (close / prev) - 1.0 : 0.0;
        }

        current_ = m;
        return current_;
    }

    std::vector<std::optional<double>>
    Momentum::compute(const std::vector<double> &closes,
                      std::size_t period, Mode mode)
    {
        std::vector<std::optional<double>> out;
        out.reserve(closes.size());
        Momentum mom(period, mode);
        for (double c : closes)
            out.emplace_back(mom.update(c));
        return out;
    }

} // namespace fin::indicators
