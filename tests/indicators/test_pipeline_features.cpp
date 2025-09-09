// tests/integration/test_pipeline_features.cpp
#include "catch2_compat.hpp"
#include <fstream>

#include "fin/io/Pipeline.hpp" // resample_csv_m1_with_stats(...)
#include "fin/indicators/FeatureBus.hpp"
#include "fin/core/Candle.hpp"

TEST_CASE("Tick CSV -> M1 Candles -> FeatureBus emits rows", "[pipeline][features]")
{
    const char *path = "ticks_features.csv";
    {
        std::ofstream f(path);
        f << "Timestamp,symbol,price,volume\n";
        // 7 minutes of rising ticks (one tick per minute at minute boundary)
        // 12:00 .. 12:06  (epoch ms; 60,000 ms per minute)
        f << "1693492800000,ABC,100.0,1\n"; // 12:00
        f << "1693492860000,ABC,101.0,1\n"; // 12:01
        f << "1693492920000,ABC,102.0,1\n"; // 12:02
        f << "1693492980000,ABC,103.0,1\n"; // 12:03
        f << "1693493040000,ABC,104.0,1\n"; // 12:04
        f << "1693493100000,ABC,105.0,1\n"; // 12:05
        f << "1693493160000,ABC,106.0,1\n"; // 12:06 (last candle will be from flush)
    }

    auto r = fin::io::resample_csv_m1_with_stats(path);
    const auto &candles = r.candles;
    REQUIRE(candles.size() >= 6);  // we expect 7 here, but >=6 is fine for emission
    REQUIRE(r.stats.skipped == 0);

    // Small-ish periods; with 7 candles, MACD(3,5,2) will be ready
    fin::indicators::FeatureBus fb(3, 3, 3, 5, 2);
    std::vector<fin::indicators::FeatureRow> rows;

    for (const auto &c : candles)
        if (auto fr = fb.update(c))
            rows.push_back(*fr);

    REQUIRE(rows.size() >= 1); // should emit once slow EMA (5) + signal (2) are ready
    const auto &last = rows.back();
    REQUIRE(last.close >= last.ema_fast);
    REQUIRE(last.rsi > 50.0);       // rising series → RSI > 50
    REQUIRE(last.macd >= 0.0);      // rising series → MACD positive
    REQUIRE(last.macd_hist >= -1e-9); // allow tiny numerical noise around zero
}
