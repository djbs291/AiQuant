#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "fin/core/Candle.hpp"
#include "fin/core/Tick.hpp"
#include "fin/signal/Signal.hpp"
#include "fin/stream/SignalSink.hpp"

namespace stream_test
{
    inline long long to_ms(const fin::core::Timestamp &ts)
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(ts.time_since_epoch()).count();
    }

    inline fin::core::Tick make_tick(long long ms, double price, const std::string &symbol = "ABC")
    {
        using namespace std::chrono;
        const fin::core::Timestamp ts{duration_cast<nanoseconds>(milliseconds{ms})};
        return fin::core::Tick(ts, fin::core::Symbol(symbol),
                               fin::core::Price(price), fin::core::Volume(1.0));
    }

    // One tick per minute, so each M1 bucket holds exactly one tick and a candle's close is
    // that tick's price. Keeps the expected event sequence obvious.
    inline std::vector<fin::core::Tick> minutely_ticks(std::size_t count, double first_price = 100.0,
                                                       const std::string &symbol = "ABC")
    {
        constexpr long long base_ms = 1693492800000LL;
        std::vector<fin::core::Tick> ticks;
        ticks.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            ticks.push_back(make_tick(base_ms + static_cast<long long>(i) * 60000,
                                      first_price + static_cast<double>(i), symbol));
        }
        return ticks;
    }

    // What a sink is allowed to keep: StreamEvent is a view, so everything is copied out here.
    struct Recorded
    {
        long long ts_ms = 0;
        fin::core::Candle candle;
        bool has_row = false;
        std::optional<double> prediction;
        fin::signal::SignalType type = fin::signal::SignalType::Hold;
        std::string symbol;
        bool partial = false;
    };

    class RecordingSink final : public fin::stream::ISignalSink
    {
    public:
        void on_signal(const fin::stream::StreamEvent &event) override
        {
            events.push_back(Recorded{to_ms(event.candle.start_time()),
                                      event.candle,
                                      event.row != nullptr,
                                      event.prediction,
                                      event.signal.type,
                                      std::string(event.symbol),
                                      event.partial});
        }

        std::vector<Recorded> events;
    };
}
