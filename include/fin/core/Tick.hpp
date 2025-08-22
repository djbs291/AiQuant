#pragma once
#ifndef FIN_CORE_TICK_HPP
#define FIN_CORE_TICK_HPP

#include "Timestamp.hpp"
#include "Symbol.hpp"
#include "Price.hpp"
#include "Volume.hpp"

namespace fin::core
{
    class Tick
    {
    public:
        Tick(Timestamp ts, Symbol sym, Price p, Volume v);

        Timestamp timestamp() const;
        const Symbol &symbol() const;
        Price price() const;
        Volume volume() const;

    private:
        Timestamp ts_;
        Symbol symbol_;
        Price price_;
        Volume volume_;
    };

} // namespace fin::core

#endif // FIN_CORE_TICK_HPP
