#include "fin/io/Resampler.hpp"
#include <chrono>

namespace fin::io
{
    using namespace std::chrono;
    using namespace fin::core;

    static nanoseconds dur_for(Timeframe tf)
    {
        switch (tf)
        {
        case Timeframe::M1:
        default:
            return minutes(1);
        }
    }

    TickToCandleResampler::TickToCandleResampler(Timeframe tf) : tf_(tf) {}

    Timestamp TickToCandleResampler::bucket_floor(Timestamp ts) const
    {
        auto d = dur_for(tf_);
        auto ns = ts.time_since_epoch();
        auto base = ns - (ns % d);
        return Timestamp(base);
    }

    Timestamp TickToCandleResampler::bucket_end(Timestamp start) const
    {
        return Timestamp(start.time_since_epoch() + dur_for(tf_));
    }

    std::optional<Candle> TickToCandleResampler::update(const Tick &t)
    {
        const auto ts = t.timestamp();
        if (!has_open_)
        {
            bucket_start_ = bucket_floor(ts);
            open_ = high_ = low_ = close_ = t.price().value();
            vol_ = t.volume().value();
            has_open_ = true;
            return std::nullopt;
        }

        // New bucket?
        if (ts >= bucket_end(bucket_start_))
        {
            Candle out{bucket_start_, Price{open_}, Price{high_}, Price{low_}, Price{close_}, Volume{vol_}};
            // start new bucket
            bucket_start_ = bucket_floor(ts);
            open_ = high_ = low_ = close_ = t.price().value();
            vol_ = t.volume().value();
            return out;
        }

        // Same bucket -> aggregate
        const double p = t.price().value();
        if (p > high_)
            high_ = p;
        if (p < low_)
            low_ = p;
        close_ = p;
        vol_ += t.volume().value();
        return std::nullopt;
    }

    std::optional<Candle> TickToCandleResampler::flush()
    {
        if (!has_open_)
            return std::nullopt;
        has_open_ = false;
        return Candle{bucket_start_, Price{open_}, Price{high_}, Price{low_}, Price{close_}, Volume{vol_}};
    }

} // namespace fin::io