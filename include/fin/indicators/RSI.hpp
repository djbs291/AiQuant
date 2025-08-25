#pragma once
#ifndef FIN_INDICATORS_RSI_HPP
#define FIN_INDICATORS_RSI_HPP

#include "fin/core/Price.hpp"
#include <cstddef>

namespace fin::indicators
{
    class RSI
    {
    public:
        explicit RSI(std::size_t period);

        void update(core::Price price);
        bool is_ready() const;
        double value() const;

    private:
        std::size_t period_;
        double avg_gain_ = 0.0;
        double avg_loss_ = 0.0;
        double last_price_ = 0.0;
        std::size_t count_ = 0;
        bool ready_ = false;
    };
} // namespace fin::indicators

#endif // FIN_INDICATORS_RSI_HPP
