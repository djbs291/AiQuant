#include <catch2/catch.hpp> // (v2 path; if you’re on v3, swap to catch_test_macros.hpp)
#include <fstream>
#include <sstream>
#include <chrono>
#if !defined(_WIN32)
#include <unistd.h>
#else
#include <windows.h>
#endif

#include "fin/io/Pipeline.hpp"
#include "fin/core/Candle.hpp"

// helper: to epoch ms from your Timestamp type
static long long to_epoch_ms(fin::core::Timestamp ts)
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(ts.time_since_epoch()).count();
}

TEST_CASE("CSV ticks -> M1 candles (OHLCV correct, boundary + EOF flush)", "[io][pipeline]")
{
    // Create a tiny tick CSV in the current working dir of the test.
    // Use a per-process unique filename to avoid collisions when tests run in parallel.
    std::ostringstream oss;
#if defined(_WIN32)
    oss << "ticks_sample_pipeline_" << GetCurrentProcessId() << ".csv";
#else
    oss << "ticks_sample_pipeline_" << getpid() << ".csv";
#endif
    const std::string path = oss.str();
    {
        std::ofstream f(path);
        f << "Timestamp,symbol,price,volume\n";
        // All UTC epoch-ms
        // Minute 12:00
        f << "1693492800000,ABC,100.0,1\n"; // open=100
        f << "1693492803000,ABC,101.5,2\n"; // high=101.5
        f << "1693492805000,ABC,99.0,3\n";  // low=99.0 (note: leading space to exercise trim)
        // Boundary tick at exactly 12:01 -> rolls first candle
        f << " 1693492860000,ABC,102.0,4\n"; // second candle (single tick)
    }

    fin::io::TickCsvOptions opt{};
    // opt.has_header = true; opt.delimiter = ','; // defaults are fine
    auto res = fin::io::resample_csv_m1_with_stats(path, opt);
    const auto &out = res.candles;

    REQUIRE(out.size() == 2);

    // Clean CSV -> no skipped rows
    REQUIRE(res.stats.skipped == 0);

    // First candle (12:00:00.000)
    REQUIRE(to_epoch_ms(out[0].start_time()) == 1693492800000LL);
    REQUIRE(out[0].open().value() == Approx(100.0));
    REQUIRE(out[0].high().value() == Approx(101.5));
    REQUIRE(out[0].low().value() == Approx(99.0));
    REQUIRE(out[0].close().value() == Approx(99.0)); // last price within 12:00
    REQUIRE(out[0].volume().value() == Approx(6.0)); // 1+2+3

    // Second candle (12:01:00.000) — single tick
    REQUIRE(to_epoch_ms(out[1].start_time()) == 1693492860000LL);
    REQUIRE(out[1].open().value() == Approx(102.0));
    REQUIRE(out[1].high().value() == Approx(102.0));
    REQUIRE(out[1].low().value() == Approx(102.0));
    REQUIRE(out[1].close().value() == Approx(102.0));
    REQUIRE(out[1].volume().value() == Approx(4.0));
}
