#include "catch2_compat.hpp"

#include <string>

#include "TestTempFiles.hpp"
#include "fin/io/Pipeline.hpp"

namespace
{
    // ABC around 100 and XYZ around 900, one of each inside every minute bucket. Blending them
    // is impossible to miss: the bar would open on one instrument and close on the other.
    std::string two_symbol_csv(int minutes = 5)
    {
        std::string csv = "Timestamp,symbol,price,volume\n";
        constexpr long long base_ms = 1693492800000LL;
        for (int i = 0; i < minutes; ++i)
        {
            const long long minute = base_ms + static_cast<long long>(i) * 60000;
            csv += std::to_string(minute) + ",ABC," + std::to_string(100 + i) + ",1\n";
            csv += std::to_string(minute + 1000) + ",XYZ," + std::to_string(900 + i) + ",1\n";
        }
        return csv;
    }
}

TEST_CASE("A two-symbol file no longer blends into one candle series", "[io][symbol]")
{
    // Issue 14. Before this, every bar came out as open=100+i, high=900+i, low=100+i,
    // close=900+i: one instrument's open against another's close, with a high and a low that
    // straddled both. The symbol was parsed off each tick and then dropped.
    const test_files::TempFile file("aiquant_two_symbols_", ".csv", two_symbol_csv());

    const auto result = fin::io::resample_csv_m1_with_stats(file.string());

    REQUIRE(result.error.empty());
    REQUIRE(result.symbol == "ABC"); // bound to the first tick in the file
    REQUIRE(result.ticks_other_symbol == 5);
    REQUIRE(result.candles.size() == 5);

    for (std::size_t i = 0; i < result.candles.size(); ++i)
    {
        const auto &candle = result.candles[i];
        const double expected = 100.0 + static_cast<double>(i);
        // One tick per symbol per bucket, so the ABC bar is flat on that one price. If XYZ
        // were still being merged in, high would be 900-something.
        REQUIRE(candle.open().value() == Approx(expected).margin(1e-9));
        REQUIRE(candle.high().value() == Approx(expected).margin(1e-9));
        REQUIRE(candle.low().value() == Approx(expected).margin(1e-9));
        REQUIRE(candle.close().value() == Approx(expected).margin(1e-9));
        REQUIRE(candle.volume().value() == Approx(1.0).margin(1e-9));
    }
}

TEST_CASE("Naming a symbol selects it out of the file", "[io][symbol]")
{
    const test_files::TempFile file("aiquant_two_symbols_pick_", ".csv", two_symbol_csv());

    const auto result = fin::io::resample_csv_m1_with_stats(file.string(), {}, "XYZ");

    REQUIRE(result.symbol == "XYZ");
    REQUIRE(result.ticks_other_symbol == 5);
    REQUIRE(result.candles.size() == 5);
    REQUIRE(result.candles.front().close().value() == Approx(900.0).margin(1e-9));
    REQUIRE(result.candles.back().close().value() == Approx(904.0).margin(1e-9));
}

TEST_CASE("Naming a symbol that is not in the file yields nothing, not the wrong thing", "[io][symbol]")
{
    const test_files::TempFile file("aiquant_two_symbols_absent_", ".csv", two_symbol_csv());

    const auto result = fin::io::resample_csv_m1_with_stats(file.string(), {}, "NOPE");

    REQUIRE(result.symbol == "NOPE");
    REQUIRE(result.candles.empty());
    REQUIRE(result.ticks_other_symbol == 10); // every tick belonged to someone else
}

TEST_CASE("A single-symbol file is unaffected by the filtering", "[io][symbol]")
{
    // The path every existing scenario takes: no symbol named, one symbol present, so the
    // binding is invisible and nothing is skipped.
    std::string csv = "Timestamp,symbol,price,volume\n";
    constexpr long long base_ms = 1693492800000LL;
    for (int i = 0; i < 4; ++i)
        csv += std::to_string(base_ms + static_cast<long long>(i) * 60000) + ",ABC," +
               std::to_string(100 + i) + ",1\n";

    const test_files::TempFile file("aiquant_one_symbol_", ".csv", csv);
    const auto result = fin::io::resample_csv_m1_with_stats(file.string());

    REQUIRE(result.symbol == "ABC");
    REQUIRE(result.ticks_other_symbol == 0);
    REQUIRE(result.candles.size() == 4);
    REQUIRE(result.stats.parsed == 4);
    REQUIRE(result.stats.rows == result.stats.parsed + result.stats.skipped);
}
