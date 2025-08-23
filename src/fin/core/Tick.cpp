#include "fin/core/Tick.hpp"

namespace fin::core
{
    Tick::Tick(Timestamp ts, Symbol sym, Price p, Volume v)
        : ts_(ts), symbol_(std::move(sym)), price_(p), volume_(v) {}

    Timestamp Tick::timestamp() const { return ts_; }
    const Symbol &Tick::symbol() const { return symbol_; }
    Price Tick::price() const { return price_; }
    Volume Tick::volume() const { return volume_; }

} // namespace fin::core
