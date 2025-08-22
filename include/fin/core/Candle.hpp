#pragma once
#ifndef FIN_CORE_CANDLE_HPP
#define FIN_CORE_CANDLE_HPP

#ifndef FIN_CORE_CANDLE_HPP
#define FIN_CORE_CANDLE_HPP

#include "Timestamp.hpp"
#include "Price.hpp"
#include "Volume.hpp"

namespace fin::core
{
    class Candle
    {
        Candle(Timestamp start, Price open, Price high, Price low, Price close, Volume vol);

        Timestamp start_time() const;
        Timestamp open() const;
        Timestamp high() const;
        Timestamp low() const;
        Timestamp close() const;
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
