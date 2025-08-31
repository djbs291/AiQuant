#include <catch2/catch.hpp>

#include "fin/io/CsvReaders.hpp"
#include "fin/core/Tick.hpp"
#include "fin/core/Candle.hpp"
#include "fin/core/Timestamp.hpp"
#include "fin/core/Symbol.hpp"
#include "fin/core/Price.hpp"
#include "fin/core/Volume.hpp"

#include <fstream>
#include <cstdio>
#include <chrono>

using namespace fin::io;
using namespace fin::core;
using namespace std::chrono;

static inline Timestamp ms(long long v)
{
    return Timestamp{milliseconds{v}};
}

TEST_CASE("Tick CSV reader parses rows and skips header/malformed")
{
    const char *path = "ticks_tmp.csv";
    {
        std::ofstream f(path);
        f << "timestamp,symbol,price,volume\n";
        f << "0,TEST,10.0,2\n";
        f << "500,TEST,12.0,3\n";
        f << "bad,line,should,skip\n";
        f << "1000,TEST,11.0,5\n";
    }

    std::vector<Tick> ticks;
    auto n = read_ticks_csv(path, ticks);
    REQUIRE(n == 3);
    REQUIRE(ticks.size() == 3);

    REQUIRE(ticks[0].timestamp() == ms(0));
    REQUIRE(ticks[0].symbol().value() == std::string("TEST"));
    REQUIRE(ticks[0].price().value() == Approx(10.0));
    REQUIRE(ticks[0].volume().value() == Approx(2.0));

    REQUIRE(ticks[1].timestamp() == ms(500));
    REQUIRE(ticks[1].price().value() == Approx(12.0));
    REQUIRE(ticks[1].volume().value() == Approx(3.0));

    REQUIRE(ticks[2].timestamp() == ms(1000));
    REQUIRE(ticks[2].price().value() == Approx(11.0));
    REQUIRE(ticks[2].volume().value() == Approx(5.0));

    std::remove(path);
}

TEST_CASE("Candle CSV reader parses rows and skips header/malformed")
{
    const char *path = "candles_tmp.csv";
    {
        std::ofstream f(path);
        f << "timestamp,open,high,low,close,volume\n";
        f << "0,10,12,9,11,100\n";
        f << "oops,not,a,candle,row,here\n";
        f << "1000,11,13,10,12,200\n";
    }

    std::vector<Candle> cs;
    auto n = fin::io::read_candles_csv(path, cs);
    REQUIRE(n == 2);
    REQUIRE(cs.size() == 2);

    REQUIRE(cs[0].start_time() == ms(0));
    REQUIRE(cs[0].open().value() == Approx(10));
    REQUIRE(cs[0].high().value() == Approx(12));
    REQUIRE(cs[0].low().value() == Approx(9));
    REQUIRE(cs[0].close().value() == Approx(11));
    REQUIRE(cs[0].volume().value() == Approx(100));

    REQUIRE(cs[1].start_time() == ms(1000));
    REQUIRE(cs[1].close().value() == Approx(12));
    REQUIRE(cs[1].volume().value() == Approx(200));

    std::remove(path);
}

TEST_CASE("FileTickSource streams ticks sequentially and resets")
{
    const char *path = "ticks_stream_tmp.csv";
    {
        std::ofstream f(path);
        f << "timestamp,symbol,price,volume\n";
        f << "0,TEST,10.0,2\n";
        f << "500,TEST,12.0,3\n";
    }

    FileTickSource src(path);

    auto a = src.next();
    REQUIRE(a.has_value());
    REQUIRE(a->timestamp() == ms(0));
    REQUIRE(a->price().value() == Approx(10.0));
    REQUIRE(a->volume().value() == Approx(2.0));

    auto b = src.next();
    REQUIRE(b.has_value());
    REQUIRE(b->timestamp() == ms(500));

    auto c = src.next();
    REQUIRE_FALSE(c.has_value()); // EOF

    src.reset();
    auto d = src.next();
    REQUIRE(d.has_value());
    REQUIRE(d->timestamp() == ms(0));

    std::remove(path);
}
