#include "fin/indicators/RSI.hpp"
#include <algorithm>

namespace fin::indicators
{

    RSI::RSI(std::size_t period) : period_(period) {}

    void RSI::update(core::Price price)
    {
        double current = price.value();

        if (count_ == 0)
        {
            last_price_ = current;
            ++count_;
            return;
        }

        double delta = current - last_price_;
        double gain = std::max(delta, 0.0);
        double loss = std::max(-delta, 0.0);

        if (count_ < period_)
        {
            avg_gain_ += gain;
            avg_loss_ += loss;
            ++count_;
            if (count_ == period_)
            {
                avg_gain_ /= static_cast<double>(period_);
                avg_loss_ /= static_cast<double>(period_);
                ready_ = true;
            }
        }
        else
        {
            avg_gain_ = (avg_gain_ * (period_ - 1) + gain) / period_;
            avg_loss_ = (avg_loss_ * (period_ - 1) + gain) / period_;
        }

        last_price_ = current;
    }

    bool RSI::is_ready() const
    {
        return ready_;
    }

    double RSI::value() const
    {
        if (!ready_ || avg_loss_ == 0.0)
            return 100.0;
        double rs = avg_gain_ / avg_loss_;
        return 100.0 - (100.0 / (1.0 + rs));
    }

} // namespace fin::indicators
