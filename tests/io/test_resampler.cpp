#include "catch2_compat.hpp"
#include <vector>
#include <chrono>

// IO under test
#include "fin/io/Resampler.hpp"

// Core types (adjust includes to your layout)
#include "fin/core/Tick.hpp"
#include "fin/core/Candle.hpp"

using namespace fin;

// --- Small helpers (adapt here if your core types differ) ---
static core::Timestamp mk_ts_ms(long long ms)
{
    using namespace std::chrono;
    return core::Timestamp(time_point<std::chrono::system_clock, nanoseconds>(nanoseconds{ms * 1'000'000LL}));
}
static core::Tick mk_tick(long long ms, double price, double volume = 1.0, const std::string &sym = "ABC")
{
    return core::Tick{
        mk_ts_ms(ms),
        core::Symbol{sym},
        core::Price{price},
        core::Volume{volume}};
}
static long long to_epoch_ms(core::Timestamp ts)
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(ts.time_since_epoch()).count();
}
// -------------------------------------------------------------

TEST_CASE("Resampler aggregates ticks within the same minute", "[io][resampler]")
{
    io::TickToCandleResampler r(io::Timeframe::M1);
    // 12:00:00.000, 12:00:03.000, 12:00:05.000 (same minute)
    auto c1 = r.update(mk_tick(1693492800000LL, 100.0, 1.0));
    auto c2 = r.update(mk_tick(1693492803000LL, 101.5, 2.0));
    auto c3 = r.update(mk_tick(1693492805000LL, 99.0, 3.0));

    // No candle until the bucket rolls or we flush
    REQUIRE_FALSE(c1.has_value());
    REQUIRE_FALSE(c2.has_value());
    REQUIRE_FALSE(c3.has_value());

    // EOF → flush partial candle
    auto last = r.flush();
    REQUIRE(last.has_value());

    const auto &bar = *last;
    REQUIRE(bar.open().value() == Approx(100.0));
    REQUIRE(bar.high().value() == Approx(101.5));
    REQUIRE(bar.low().value() == Approx(99.0));
    REQUIRE(bar.close().value() == Approx(99.0));
    REQUIRE(bar.volume().value() == Approx(6.0)); // 1+2+3

    // Timestamp should be the minute floor (12:00:00.000)
    REQUIRE(to_epoch_ms(bar.start_time()) == 1693492800000LL);
}

TEST_CASE("Resampler emits on boundary roll (next tick crosses minute)", "[io][resampler]")
{
    io::TickToCandleResampler r(io::Timeframe::M1);

    // First minute (12:00)
    r.update(mk_tick(1693492800000LL, 100.0, 1.0));
    r.update(mk_tick(1693492803000LL, 101.5, 2.0));
    r.update(mk_tick(1693492805000LL, 99.0, 3.0));

    // Next tick at 12:01:00.000 (exact boundary) → should emit 12:00 bar
    auto rolled = r.update(mk_tick(1693492860000LL, 102.0, 4.0));
    REQUIRE(rolled.has_value());

    const auto &b0 = *rolled;
    REQUIRE(b0.open().value() == Approx(100.0));
    REQUIRE(b0.high().value() == Approx(101.5));
    REQUIRE(b0.low().value() == Approx(99.0));
    REQUIRE(b0.close().value() == Approx(99.0));
    REQUIRE(b0.volume().value() == Approx(6.0));
    REQUIRE(to_epoch_ms(b0.start_time()) == 1693492800000LL);

    // Now we are inside the 12:01 bucket with a single tick so far
    auto tail = r.flush();
    REQUIRE(tail.has_value());
    const auto &b1 = *tail;
    REQUIRE(b1.open().value() == Approx(102.0));
    REQUIRE(b1.high().value() == Approx(102.0));
    REQUIRE(b1.low().value() == Approx(102.0));
    REQUIRE(b1.close().value() == Approx(102.0));
    REQUIRE(b1.volume().value() == Approx(4.0));
    REQUIRE(to_epoch_ms(b1.start_time()) == 1693492860000LL); // 12:01:00.000
}

TEST_CASE("Resampler handles gaps (no empty-bar fill in MVP)", "[io][resampler]")
{
    io::TickToCandleResampler r(io::Timeframe::M1);

    // Tick in 12:00
    r.update(mk_tick(1693492800000LL, 100.0, 1.0));
    // Next tick jumps to 12:05 → roll 12:00, start 12:05 bucket
    auto rolled = r.update(mk_tick(1693493100000LL, 110.0, 2.0)); // +5 minutes

    REQUIRE(rolled.has_value());
    REQUIRE(to_epoch_ms(rolled->start_time()) == 1693492800000LL);

    // Flush 12:05 partial
    auto last = r.flush();
    REQUIRE(last.has_value());
    REQUIRE(to_epoch_ms(last->start_time()) == 1693493100000LL);

    // We produced exactly 2 candles; no filler bars for 12:01..12:04 (by design in MVP)
    // (Count verified implicitly by the two optionals we checked.)
}

TEST_CASE("Exact-boundary tick behavior (>= bucket_end rolls)", "[io][resampler]")
{
    io::TickToCandleResampler r(io::Timeframe::M1);

    // One tick at 12:00:59.999 → still same bucket
    r.update(mk_tick(1693492859999LL, 200.0, 1.0));
    // Next tick at 12:01:00.000 → must roll
    auto rolled = r.update(mk_tick(1693492860000LL, 201.0, 1.0));
    REQUIRE(rolled.has_value());
    REQUIRE(to_epoch_ms(rolled->start_time()) == 1693492800000LL);
    REQUIRE(rolled->open().value() == Approx(200.0));
    REQUIRE(rolled->close().value() == Approx(200.0));

    // Flush shows the 12:01 bar
    auto last = r.flush();
    REQUIRE(last.has_value());
    REQUIRE(to_epoch_ms(last->start_time()) == 1693492860000LL);
    REQUIRE(last->open().value() == Approx(201.0));
}

TEST_CASE("Flush idempotency & empty-stream behavior", "[io][resampler]")
{
    io::TickToCandleResampler r(io::Timeframe::M1);

    // No ticks → flush should be empty
    auto none = r.flush();
    REQUIRE_FALSE(none.has_value());

    // Feed one tick, then flush twice
    r.update(mk_tick(1693492800000LL, 42.0, 1.0));
    auto once = r.flush();
    REQUIRE(once.has_value());
    // Second flush should yield nothing
    auto twice = r.flush();
    REQUIRE_FALSE(twice.has_value());
}

TEST_CASE("Aggregates volume & extremes correctly with many ticks", "[io][resampler]")
{
    io::TickToCandleResampler r(io::Timeframe::M1);

    // Minute 12:00 with messy sequence
    r.update(mk_tick(1693492800000LL, 100.0, 0.5));
    r.update(mk_tick(1693492801000LL, 99.5, 1.2));
    r.update(mk_tick(1693492802000LL, 101.2, 0.3));
    r.update(mk_tick(1693492803000LL, 100.8, 2.0));
    r.update(mk_tick(1693492804000LL, 100.1, 0.0));               // zero-volume edge
    auto rolled = r.update(mk_tick(1693492860000LL, 100.0, 1.0)); // boundary → emit

    REQUIRE(rolled.has_value());
    const auto &b = *rolled;
    REQUIRE(b.open().value() == Approx(100.0));
    REQUIRE(b.high().value() == Approx(101.2));
    REQUIRE(b.low().value() == Approx(99.5));
    REQUIRE(b.close().value() == Approx(100.1)); // last price within the minute
    REQUIRE(b.volume().value() == Approx(4.0));  // 0.5+1.2+0.3+2.0+0.0
}
