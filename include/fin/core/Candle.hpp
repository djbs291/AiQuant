#pragma once
#ifndef FIN_CORE_CANDLE_HPP
#define FIN_CORE_CANDLE_HPP

#include "Timestamp.hpp"
#include "Price.hpp"
#include "Volume.hpp"
#include <string>

namespace fin::core
{
    class Candle
    {
    public:
        Candle(Timestamp start, Price open, Price high, Price low, Price close, Volume vol);

        Timestamp start_time() const;
        Price open() const;
        Price high() const;
        Price low() const;
        Price close() const;
        Volume volume() const;

    private:
        Timestamp start_;
        Price open_;
        Price high_;
        Price low_;
        Price close_;
        Volume volume_;
    };

} // namespace fin::core

#endif /* FIN_CORE_CANDLE_HPP */
