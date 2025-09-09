// tests/io/test_tick_to_candle.cpp
#include "catch2_compat.hpp"
#include <fstream>
#include <vector>

#include "fin/io/Sources.hpp"
#include "fin/io/Resampler.hpp"
#include "fin/core/Tick.hpp"
#include "fin/core/Candle.hpp"

using namespace fin;

TEST_CASE("Tick -> 1m candle resampling (EOF flush, boundary roll)", "[io][resampler]")
{
    const char *path = "ticks_sample.csv";
    {
        std::ofstream f(path);
        f << "Timestamp,symbol,price,volume\n";
        // 12:00:00.xxx to 12:00:59.xxx (same minute)
        f << "1693492800000,ABC,100.0,1\n";
        f << "1693492803000,ABC,101.5,2\n";
        f << "1693492805000,ABC,99.0,3\n";
        // next minute -> should emit the first candle
        f << "1693492860000,ABC,102.0,4\n"; // 12:01:00 (boundary roll)
    }

    io::FileTickSource src(path, {});
    io::TickToCandleResampler res(io::Timeframe::M1);

    std::vector<core::Candle> out;
    while (auto t = src.next())
    {
        if (auto c = res.update(*t))
            out.push_back(*c);
    }
    if (auto c = res.flush())
        out.push_back(*c);

    REQUIRE(out.size() == 2);

    // First minute OHLCV
    REQUIRE(out[0].open().value() == Approx(100.0));
    REQUIRE(out[0].high().value() == Approx(101.5));
    REQUIRE(out[0].low().value() == Approx(99.0));
    REQUIRE(out[0].close().value() == Approx(99.0));
    REQUIRE(out[0].volume().value() == Approx(6.0)); // 1+2+3

    // Second minute single-tick candle
    REQUIRE(out[1].open().value() == Approx(102.0));
    REQUIRE(out[1].high().value() == Approx(102.0));
    REQUIRE(out[1].low().value() == Approx(102.0));
    REQUIRE(out[1].close().value() == Approx(102.0));
    REQUIRE(out[1].volume().value() == Approx(4.0));
}
