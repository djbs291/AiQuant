#include <catch2/catch.hpp>

#include "fin/io/Resampler.hpp"
#include "fin/core/Tick.hpp"
#include "fin/core/Candle.hpp"
#include "fin/core/Timestamp.hpp"
#include "fin/core/Symbol.hpp"
#include "fin/core/Price.hpp"
#include "fin/core/Volume.hpp"

#include <chrono>

using namespace fin::io;
using namespace fin::core;
using namespace std::chrono;

static inline Timestamp ms(long long v)
{
    // Your Timestamp is nanoseconds-based; constructing from milliseconds is OK.
    return Timestamp{milliseconds{v}};
}

TEST_CASE("Resampler aggregates ticks into one candle and rolls on boundary")
{
    Resampler rs(Resampler::Duration{1000}); // 1s bars

    // Window [0,1000)
    Tick t1{ms(100), Symbol{"TEST"}, Price{10.0}, Volume{2.0}};
    Tick t2{ms(600), Symbol{"TEST"}, Price{12.0}, Volume{3.0}};
    Tick t3{ms(999), Symbol{"TEST"}, Price{11.0}, Volume{5.0}};

    // Next window [1000,2000)
    Tick t4{ms(1000), Symbol{"TEST"}, Price{13.0}, Volume{7.0}};

    REQUIRE_FALSE(rs.update(t1).has_value());
    REQUIRE_FALSE(rs.update(t2).has_value());
    REQUIRE_FALSE(rs.update(t3).has_value());

    // This tick at exactly 1000ms rolls the [0,1000) candle
    auto c1 = rs.update(t4);
    REQUIRE(c1.has_value());

    REQUIRE(c1->start_time() == ms(0));
    REQUIRE(c1->open().value() == Approx(10.0));
    REQUIRE(c1->high().value() == Approx(12.0));
    REQUIRE(c1->low().value() == Approx(10.0));
    REQUIRE(c1->close().value() == Approx(11.0));
    REQUIRE(c1->volume().value() == Approx(2.0 + 3.0 + 5.0));

    // Flush last partial candle [1000,2000) containing only t4
    auto c2 = rs.flush();
    REQUIRE(c2.has_value());
    REQUIRE(c2->start_time() == ms(1000));
    REQUIRE(c2->open().value() == Approx(13.0));
    REQUIRE(c2->high().value() == Approx(13.0));
    REQUIRE(c2->low().value() == Approx(13.0));
    REQUIRE(c2->close().value() == Approx(13.0));
    REQUIRE(c2->volume().value() == Approx(7.0));
}

TEST_CASE("Resampler rolls multiple windows; no empty candles are synthesized")
{
    Resampler rs(Resampler::Duration{1000}); // 1s

    // ticks at 0.1s, 0.9s (first window)
    rs.update(Tick{ms(100), Symbol{"X"}, Price{10}, Volume{1}});
    rs.update(Tick{ms(900), Symbol{"X"}, Price{11}, Volume{2}});

    // next tick jumps to 2.5s → should emit the [0,1000) candle,
    // open [2000,3000) directly (no empty [1000,2000) candle is synthesized)
    auto c1 = rs.update(Tick{ms(2500), Symbol{"X"}, Price{15}, Volume{4}});
    REQUIRE(c1.has_value());
    REQUIRE(c1->start_time() == ms(0));
    REQUIRE(c1->open().value() == Approx(10));
    REQUIRE(c1->high().value() == Approx(11));
    REQUIRE(c1->low().value() == Approx(10));
    REQUIRE(c1->close().value() == Approx(11));
    REQUIRE(c1->volume().value() == Approx(3));

    // Flush the partial [2000,3000) candle with single tick at 2.5s
    auto c2 = rs.flush();
    REQUIRE(c2.has_value());
    REQUIRE(c2->start_time() == ms(2000));
    REQUIRE(c2->open().value() == Approx(15));
    REQUIRE(c2->high().value() == Approx(15));
    REQUIRE(c2->low().value() == Approx(15));
    REQUIRE(c2->close().value() == Approx(15));
    REQUIRE(c2->volume().value() == Approx(4));
}
