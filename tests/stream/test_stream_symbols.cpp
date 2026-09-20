#include "catch2_compat.hpp"

#include <stdexcept>
#include <string>
#include <vector>

#include "fin/io/MockTickSource.hpp"
#include "fin/stream/StreamEngine.hpp"
#include "stream/TestStreamHelpers.hpp"

using fin::stream::StreamConfig;
using fin::stream::StreamEngine;
using fin::stream::SymbolPolicy;
using stream_test::make_tick;
using stream_test::minutely_ticks;
using stream_test::RecordingSink;

namespace
{
    // ABC and XYZ interleaved in the same minute buckets, the shape of a real multi-symbol
    // export. The batch path blends these into one candle series without a word.
    std::vector<fin::core::Tick> interleaved_ticks(std::size_t minutes)
    {
        constexpr long long base_ms = 1693492800000LL;
        std::vector<fin::core::Tick> ticks;
        for (std::size_t i = 0; i < minutes; ++i)
        {
            const long long minute = base_ms + static_cast<long long>(i) * 60000;
            ticks.push_back(make_tick(minute, 100.0 + static_cast<double>(i), "ABC"));
            // A wildly different price, so any blending shows up immediately in the closes.
            ticks.push_back(make_tick(minute + 1000, 900.0 + static_cast<double>(i), "XYZ"));
        }
        return ticks;
    }
}

TEST_CASE("A second symbol is refused rather than blended", "[stream][symbols]")
{
    fin::io::MockTickSource source(interleaved_ticks(5));

    StreamConfig cfg{}; // SymbolPolicy::Reject is the default
    StreamEngine engine(cfg);

    std::string message;
    try
    {
        engine.run(source);
    }
    catch (const std::invalid_argument &ex)
    {
        message = ex.what();
    }

    // The error has to name both symbols, or the user cannot tell which file surprised them.
    REQUIRE(message.find("ABC") != std::string::npos);
    REQUIRE(message.find("XYZ") != std::string::npos);
    REQUIRE(message.find("--symbol") != std::string::npos);
}

TEST_CASE("Selecting one symbol yields exactly that symbol's stream", "[stream][symbols]")
{
    // The proof that the blending bug is fixed: filtering a mixed file has to produce the
    // very same candles as a file that only ever held the wanted symbol.
    fin::io::MockTickSource mixed(interleaved_ticks(8));
    StreamConfig filtered{};
    filtered.symbol = "ABC";
    filtered.foreign_symbol = SymbolPolicy::Skip;

    RecordingSink filtered_sink;
    StreamEngine filtered_engine(filtered, nullptr, &filtered_sink);
    const auto filtered_stats = filtered_engine.run(mixed);

    fin::io::MockTickSource clean(minutely_ticks(8));
    RecordingSink clean_sink;
    StreamEngine clean_engine(StreamConfig{}, nullptr, &clean_sink);
    const auto clean_stats = clean_engine.run(clean);

    REQUIRE(filtered_engine.bound_symbol() == "ABC");
    REQUIRE(filtered_sink.events.size() == clean_sink.events.size());
    REQUIRE(filtered_stats.candles == clean_stats.candles);

    for (std::size_t i = 0; i < filtered_sink.events.size(); ++i)
    {
        REQUIRE(filtered_sink.events[i].ts_ms == clean_sink.events[i].ts_ms);
        REQUIRE(filtered_sink.events[i].candle.close().value() ==
                Approx(clean_sink.events[i].candle.close().value()).margin(1e-12));
        REQUIRE(filtered_sink.events[i].symbol == "ABC");
    }

    // Every XYZ tick was dropped on the way in, and counted rather than forgotten.
    REQUIRE(filtered_stats.ticks_other_symbol == 8);
}

TEST_CASE("Out-of-order ticks are dropped and counted", "[stream][symbols]")
{
    // The resampler already drops these by a stated MVP policy, but silently. The pipeline
    // checks first so the drop becomes a number the caller can see.
    auto ticks = minutely_ticks(6);
    constexpr long long base_ms = 1693492800000LL;
    ticks.push_back(make_tick(base_ms + 60000, 500.0)); // far behind the last accepted tick
    ticks.push_back(make_tick(base_ms + 120000, 501.0));

    fin::io::MockTickSource source(std::move(ticks));
    RecordingSink sink;
    StreamEngine engine(StreamConfig{}, nullptr, &sink);
    const auto stats = engine.run(source);

    REQUIRE(stats.ticks == 8);
    REQUIRE(stats.ticks_out_of_order == 2);

    // The stragglers changed nothing: the same six one-tick minutes came out the far end.
    REQUIRE(sink.events.size() == 6);
    REQUIRE(sink.events.back().candle.close().value() == Approx(105.0).margin(1e-12));
}
