#include "fin/io/Resampler.hpp"
#include <algorithm>

namespace fin::io
{

    using namespace fin::core;

    static inline long long to_ns_count(const Timestamp &ts)
    {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(ts.time_since_epoch()).count();
    }

    Timestamp Resampler::floor_ts(Timestamp ts) const
    {
        using namespace std::chrono;
        const auto ns = duration_cast<nanoseconds>(ts.time_since_epoch()).count();
        const auto step = duration_cast<nanoseconds>(interval_).count();
        const auto flo = (ns / step) * step;
        return Timestamp{nanoseconds(static_cast<long long>(flo))};
    }

    Candle Resampler::make_candle() const
    {
        // Candle(Timestamp start, Price open, Price high, Price low, Price close, Volume vol)
        return Candle(window_start_, open_, high_, low_, close_, vol_);
    }

    void Resampler::open_new(Timestamp start, const Tick &t)
    {
        have_open_ = true;
        window_start_ = start;
        const Price p = t.price();
        open_ = p;
        high_ = p;
        low_ = p;
        close_ = p,
        vol_ = t.volume();
    }

    void Resampler::accumulate(const Tick &t)
    {
        const Price p = t.price();
        if (high_ < p)
            high_ = p; // Price defines operator<
        if (p < low_)
            low_ = p;
        close_ = p;
        vol_ = vol_ + t.volume(); // Volume defines operator+
    }

    std::optional<Candle> Resampler::update(const Tick &t)
    {
        const auto start = floor_ts(t.timestamp());
        if (!have_open_)
        {
            open_new(start, t);
            return std::nullopt;
        }

        if (start == window_start_)
        {
            accumulate(t);
            return std::nullopt;
        }

        // Roll window: emit previous, seed next with current tick
        Candle finished = make_candle();
        open_new(start, t);
        return finished;
    }

    std::optional<Candle> Resampler::flush()
    {
        if (!have_open_)
            return std::nullopt;
        have_open_ = false;
        return make_candle();
    }

} // namespace fin::io