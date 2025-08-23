#include "fin/core/Candle.hpp"

namespace fin::core
{
    Candle::Candle(Timestamp start, Price open, Price high, Price low, Price close, Volume vol)
        : start_(start), open_(open), high_(high), low_(low), close_(close), volume_(vol) {}

    Timestamp Candle::start_time() const { return start_; }
    Price Candle::open() const { return open_; }
    Price Candle::high() const { return high_; }
    Price Candle::low() const { return low_; }
    Price Candle::close() const { return close_; }
    Volume Candle::volume() const { return volume_; }
} // namespace fin::core
