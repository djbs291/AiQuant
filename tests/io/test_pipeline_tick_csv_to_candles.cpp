#include "catch2_compat.hpp"
#include "TestTempFiles.hpp"
#include <chrono>

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
    // Tiny tick CSV in a unique temp file. All UTC epoch-ms.
    const test_files::TempFile fixture("aiquant_ticks_pipeline_", ".csv",
                                       "Timestamp,symbol,price,volume\n"
                                       // Minute 12:00
                                       "1693492800000,ABC,100.0,1\n" // open=100
                                       "1693492803000,ABC,101.5,2\n" // high=101.5
                                       "1693492805000,ABC,99.0,3\n"  // low=99.0
                                       // Boundary tick at exactly 12:01 -> rolls first candle.
                                       // Leading space exercises the trim.
                                       " 1693492860000,ABC,102.0,4\n"); // second candle (single tick)

    fin::io::TickCsvOptions opt{};
    // opt.has_header = true; opt.delimiter = ','; // defaults are fine
    auto res = fin::io::resample_csv_m1_with_stats(fixture.string(), opt);
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
