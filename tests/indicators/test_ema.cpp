#include <catch2/catch.hpp>
#include "fin/indicators/EMA.hpp"
#include <vector>
#include <optional>
#include <cmath>

using fin::indicators::EMA;

TEST_CASE("EMA warms up using SMA seed")
{
    const std::size_t P = 5;
    EMA ema(P);

    // First P-1 updates -> nullopt
    for (std::size_t i = 0; i < P - 1; ++i)
    {
        auto v = ema.update(10.0);
        REQUIRE_FALSE(v.has_value());
    }

    // P-th update yields SMA = 10
    auto v = ema.update(10.0);
    REQUIRE(v.has_value());
    REQUIRE(*v == Approx(10.0));
    REQUIRE(ema.value().has_value());
}

TEST_CASE("EMA exponential smoothing matches recurrence")
{
    // Simple sequence with known behavior
    const std::size_t P = 3; // alpha = 0.5
    EMA ema(P);

    // Warmup seed: SMA of first 3: (10 + 11 + 13) / 3 = 34/3
    REQUIRE_FALSE(ema.update(10.0).has_value());
    REQUIRE_FALSE(ema.update(11.0).has_value());
    auto seed = ema.update(13.0);
    REQUIRE(seed.has_value());
    REQUIRE(*seed == Approx((10.0 + 11.0 + 13.0) / 3.0));

    // Next point: x=12, alpha=2/(3+1)=0.5
    // EMA = 0.5*12 + 0.5*seed
    auto v1 = ema.update(12.0);
    REQUIRE(v1.has_value());
    REQUIRE(*v1 == Approx(0.5 * 12.0 + 0.5 * *seed));

    // Next: x=14
    auto v2 = ema.update(14.0);
    REQUIRE(v2.has_value());
    REQUIRE(*v2 == Approx(0.5 * 14.0 + 0.5 * *v1));
}

TEST_CASE("EMA batch compute equals incremental")
{
    const std::size_t P = 10;
    std::vector<double> x = {100, 101, 99, 98, 102, 103, 104, 105, 102, 101, 100, 99, 98, 97, 96, 110};

    auto batch = EMA::compute(x, P);

    EMA inc(P);
    std::vector<std::optional<double>> inc_series;
    inc_series.reserve(x.size());
    for (double v : x)
        inc_series.emplace_back(inc.update(v));

    REQUIRE(batch.size() == inc_series.size());
    for (std::size_t i = 0; i < batch.size(); ++i)
    {
        if (batch[i].has_value() || inc_series[i].has_value())
        {
            REQUIRE(batch[i].has_value());
            REQUIRE(inc_series[i].has_value());
            REQUIRE(*batch[i] == Approx(*inc_series[i]).margin(1e-12));
        }
    }
}

TEST_CASE("EMA reset clears state and warmup restarts")
{
    EMA ema(4);
    ema.update(1.0);
    ema.update(2.0);
    ema.update(3.0);
    auto v = ema.update(4.0); // seed produced here
    REQUIRE(v.has_value());

    ema.reset();
    REQUIRE_FALSE(ema.value().has_value());
    REQUIRE_FALSE(ema.update(10.0).has_value());
    REQUIRE_FALSE(ema.update(10.0).has_value());
    REQUIRE_FALSE(ema.update(10.0).has_value());
    auto v2 = ema.update(10.0);
    REQUIRE(v2.has_value());
    REQUIRE(*v2 == Approx(10.0));
}
