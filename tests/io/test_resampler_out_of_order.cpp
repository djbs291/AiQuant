// tests/io/test_resampler_out_of_order.cpp
#include "catch2_compat.hpp"
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

TEST_CASE("Out-of-order ticks are ignored (MVP)", "[io][resampler][ooo]")
{
    io::TickToCandleResampler R(io::Timeframe::M1);

    // In-order ticks (12:00:00, 12:00:01)
    R.update(tk(1693492800000LL, 100.0, 1.0)); // open=100
    R.update(tk(1693492801000LL, 101.0, 1.0)); // high=101

    // Out-of-order tick arrives late (12:00:00.500) with price=50 → should be ignored
    R.update(tk(1693492800500LL, 50.0, 1.0)); // ignored → low should NOT become 50

    // Boundary → emit first candle
    auto c0 = R.update(tk(1693492860000LL, 102.0, 1.0));
    REQUIRE(c0.has_value());
    REQUIRE(to_ms(c0->start_time()) == 1693492800000LL);
    REQUIRE(c0->open().value() == Approx(100.0));
    REQUIRE(c0->high().value() == Approx(101.0));
    REQUIRE(c0->low().value() == Approx(100.0)); // still 100; 50 was ignored
    REQUIRE(c0->close().value() == Approx(101.0));
    REQUIRE(c0->volume().value() == Approx(2.0)); // 1+1 (ignored tick doesn't add)

    // Flush partial 12:01 bar
    auto c1 = R.flush();
    REQUIRE(c1.has_value());
    REQUIRE(to_ms(c1->start_time()) == 1693492860000LL);
    REQUIRE(c1->open().value() == Approx(102.0));
}
