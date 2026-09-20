#include "catch2_compat.hpp"

#include <cmath>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "TestTempFiles.hpp"
#include "fin/io/Pipeline.hpp"
#include "fin/io/Sources.hpp"

namespace
{
    // Reads a whole file through the source and reports what came back, so each case below
    // can assert on the counters rather than on whatever the candles happened to look like.
    struct ReadOutcome
    {
        std::vector<fin::core::Tick> ticks;
        fin::io::ReadStats stats;
        std::string error;
    };

    ReadOutcome read_all(const std::string &path)
    {
        ReadOutcome out;
        fin::io::FileTickSource source(path);
        while (auto tick = source.next())
            out.ticks.push_back(*tick);
        out.stats = source.stats();
        out.error = source.error();
        return out;
    }

    std::string with_header(const std::string &rows)
    {
        return "Timestamp,symbol,price,volume\n" + rows;
    }

    // The largest epoch-millis value that still converts to nanoseconds without overflowing.
    constexpr long long kMaxEpochMs = std::numeric_limits<long long>::max() / 1'000'000LL;
}

TEST_CASE("A header missing a required column is refused by name", "[io][csv]")
{
    // This used to read out of bounds. find_idx returns -1 for the absent column, and the
    // width guard only compared the *largest* index against the row, so cols[-1] ran off the
    // front of the vector. ASan called it a heap-buffer-overflow.
    const test_files::TempFile file("aiquant_csv_no_price_", ".csv",
                                    "Timestamp,symbol,volume\n1693492800000,ABC,1\n");

    const auto outcome = read_all(file.string());

    REQUIRE(outcome.ticks.empty());
    REQUIRE(outcome.stats.parsed == 0);
    // The reason has to name the column, or the user is left guessing which one.
    REQUIRE(outcome.error.find("price") != std::string::npos);

    // And it reaches the scenario runner, which would otherwise complain about warmup.
    const auto piped = fin::io::resample_csv_m1_with_stats(file.string());
    REQUIRE(piped.candles.empty());
    REQUIRE(piped.error.find("price") != std::string::npos);
}

TEST_CASE("An unopenable file reports why rather than reading as empty", "[io][csv]")
{
    const auto missing = test_files::temp_path("aiquant_csv_absent_", ".csv");
    const auto outcome = read_all(missing.string());

    REQUIRE(outcome.ticks.empty());
    REQUIRE(outcome.error.find("open") != std::string::npos);
}

TEST_CASE("Numbers with trailing junk are rejected, not truncated", "[io][csv]")
{
    // std::from_chars stops at the first character it cannot use and still reports success,
    // so without a full-consumption check "1.5abc" read as 1.5. The INI and JSON parsers in
    // fin_app have always required the whole token; this reader had not.
    const test_files::TempFile file("aiquant_csv_trailing_", ".csv",
                                    with_header("1693492800000,ABC,1.5abc,1\n"
                                                "123xyz,ABC,100,1\n"
                                                "1693492860000,ABC,100,2volumes\n"));

    const auto outcome = read_all(file.string());

    REQUIRE(outcome.ticks.empty());
    REQUIRE(outcome.stats.rows == 3);
    REQUIRE(outcome.stats.skipped == 3);
    REQUIRE(outcome.stats.parsed == 0);
}

TEST_CASE("Non-finite prices and volumes are rejected", "[io][csv]")
{
    // from_chars accepts "nan" and "inf" by the standard's general format. A NaN price is the
    // worst value to admit: the resampler compares with > and <, both false for NaN, so the
    // bar's high and low keep the wrong values without a word.
    const test_files::TempFile file("aiquant_csv_nonfinite_", ".csv",
                                    with_header("1693492800000,ABC,nan,1\n"
                                                "1693492860000,ABC,inf,1\n"
                                                "1693492920000,ABC,-inf,1\n"
                                                "1693492980000,ABC,100,nan\n"));

    const auto outcome = read_all(file.string());

    REQUIRE(outcome.ticks.empty());
    REQUIRE(outcome.stats.rows == 4);
    REQUIRE(outcome.stats.skipped == 4);
}

TEST_CASE("Negative prices and volumes are rejected", "[io][csv]")
{
    const test_files::TempFile file("aiquant_csv_negative_", ".csv",
                                    with_header("1693492800000,ABC,-50,1\n"
                                                "1693492860000,ABC,100,-1\n"
                                                "1693492920000,ABC,100,1\n"));

    const auto outcome = read_all(file.string());

    REQUIRE(outcome.ticks.size() == 1);
    REQUIRE(outcome.stats.skipped == 2);
    REQUIRE(outcome.ticks.front().price().value() == Approx(100.0).margin(1e-12));
}

TEST_CASE("A timestamp that would overflow the nanosecond conversion is rejected", "[io][csv]")
{
    // The conversion multiplies by 1'000'000. Past this point that is signed overflow, which
    // is undefined behaviour rather than simply a large number: UBSan flagged it at
    // FileTickSource.cpp's from_epoch_ms.
    const std::string rows = std::to_string(kMaxEpochMs + 1) + ",ABC,100,1\n" +
                             std::to_string(kMaxEpochMs) + ",ABC,101,1\n" +
                             "-1,ABC,102,1\n";
    const test_files::TempFile file("aiquant_csv_bigts_", ".csv", with_header(rows));

    const auto outcome = read_all(file.string());

    // The in-range one survives; the overflowing and the negative do not.
    REQUIRE(outcome.ticks.size() == 1);
    REQUIRE(outcome.ticks.front().price().value() == Approx(101.0).margin(1e-12));
    REQUIRE(outcome.stats.skipped == 2);
}

TEST_CASE("Row counts add up: rows equals parsed plus skipped", "[io][csv]")
{
    // The header used to be counted in `rows`, so the three numbers never reconciled and a
    // reader could not tell a skipped row from a header.
    const test_files::TempFile file("aiquant_csv_counts_", ".csv",
                                    with_header("1693492800000,ABC,100,1\n"
                                                "not-a-timestamp,ABC,100,1\n"
                                                "1693492860000,ABC,101,2\n"
                                                "1693492920000,ABC,oops,1\n"));

    const auto outcome = read_all(file.string());

    REQUIRE(outcome.stats.rows == 4);
    REQUIRE(outcome.stats.parsed == 2);
    REQUIRE(outcome.stats.skipped == 2);
    REQUIRE(outcome.stats.rows == outcome.stats.parsed + outcome.stats.skipped);
}

TEST_CASE("Randomized rows never break the reader's guarantees", "[io][csv][property]")
{
    // A property test rather than a fixed fixture: whatever the file holds, the reader must
    // not crash, the counters must reconcile, and anything it does hand back must be usable.
    // Under the sanitizer CI job this doubles as a fuzz run over the parsing paths, which is
    // where the out-of-bounds read and the signed overflow above were found.
    const std::vector<std::string> tokens = {
        "1693492800000", "0", "-1", "1.5", "1.5abc", "abc", "", " 100 ", "nan", "inf", "-inf",
        "1e400", "99999999999999999999999999", "-0.0", "1,2", "\t42", "0x10", "+5", ".5", "1."};

    for (unsigned seed = 1; seed <= 8; ++seed)
    {
        std::mt19937 rng(seed); // fixed seeds: a failure is reproducible, unlike a clock seed
        std::uniform_int_distribution<std::size_t> pick(0, tokens.size() - 1);
        std::uniform_int_distribution<int> field_count(1, 5);

        std::string rows;
        constexpr int kRows = 200;
        for (int r = 0; r < kRows; ++r)
        {
            const int fields = field_count(rng);
            for (int f = 0; f < fields; ++f)
            {
                if (f > 0)
                    rows += ",";
                rows += tokens[pick(rng)];
            }
            rows += "\n";
        }

        const test_files::TempFile file("aiquant_csv_property_", ".csv", with_header(rows));
        const auto outcome = read_all(file.string());

        REQUIRE(outcome.error.empty()); // the header is well formed, so the file is usable
        REQUIRE(outcome.stats.rows == static_cast<std::size_t>(kRows));
        REQUIRE(outcome.stats.rows == outcome.stats.parsed + outcome.stats.skipped);
        REQUIRE(outcome.ticks.size() == outcome.stats.parsed);

        for (const auto &tick : outcome.ticks)
        {
            const double price = tick.price().value();
            const double volume = tick.volume().value();
            REQUIRE(std::isfinite(price));
            REQUIRE(std::isfinite(volume));
            REQUIRE(price >= 0.0);
            REQUIRE(volume >= 0.0);

            using namespace std::chrono;
            const long long ms =
                duration_cast<milliseconds>(tick.timestamp().time_since_epoch()).count();
            REQUIRE(ms >= 0);
            REQUIRE(ms <= kMaxEpochMs);
        }
    }
}
