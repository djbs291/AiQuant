// tests/io/test_resampler_m5.cpp
#include <catch2/catch.hpp>
#include "fin/io/Resampler.hpp"
#include "fin/core/Tick.hpp"
#include "fin/core/Candle.hpp"
#include <chrono>

using namespace fin;
static core::Timestamp ts_ms(long long ms)
{
    using namespace std::chrono;
    return core::Timestamp(time_point<std::chrono::system_clock, nanoseconds>(nanoseconds{ms * 1'000'000}));
}
static core::Tick tk(long long ms, double p, double v = 1.0)
{
    return core::Tick{ts_ms(ms), core::Symbol{"ABC"}, core::Price{p}, core::Volume{v}};
}
static long long to_ms(core::Timestamp t)
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(t.time_since_epoch()).count();
}

TEST_CASE("Resampler M5 buckets and roll", "[io][resampler][m5]")
{
    io::TickToCandleResampler R(io::Timeframe::M5);

    // 12:00:00..12:04:59 inside first M5 bucket
    R.update(tk(1693492800000LL, 100.0, 1.0)); // 12:00:00.000
    R.update(tk(1693492920000LL, 101.0, 1.0)); // 12:02:00.000  (+120,000 ms)
    R.update(tk(1693493040000LL, 99.0, 1.0));  // 12:04:00.000  (+240,000 ms)

    // Boundary tick at 12:05:00.000 → emit first candle
    auto c0 = R.update(tk(1693493100000LL, 102.0, 4.0)); // 12:05:00.000 (boundary → emit)
    REQUIRE(c0.has_value());
    REQUIRE(to_ms(c0->start_time()) == 1693492800000LL); // first bucket starts at 12:00:00
    REQUIRE(c0->open().value() == Approx(100.0));
    REQUIRE(c0->high().value() == Approx(101.0));
    REQUIRE(c0->low().value() == Approx(99.0));
    REQUIRE(c0->close().value() == Approx(99.0));
    REQUIRE(c0->volume().value() == Approx(3.0));

    // Flush last partial (12:05 bucket, single tick)
    auto c1 = R.flush();
    REQUIRE(c1.has_value());
    REQUIRE(to_ms(c1->start_time()) == 1693493100000LL);
    REQUIRE(c1->open().value() == Approx(102.0));
    REQUIRE(c1->high().value() == Approx(102.0));
    REQUIRE(c1->low().value() == Approx(102.0));
    REQUIRE(c1->close().value() == Approx(102.0));
    REQUIRE(c1->volume().value() == Approx(4.0));
}
