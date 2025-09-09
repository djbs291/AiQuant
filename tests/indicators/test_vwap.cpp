#include "catch2_compat.hpp"
#include "fin/indicators/VWAP.hpp"
#include <vector>
#include <optional>

using fin::indicators::VWAP;

TEST_CASE("VWAP accumulates price*volume / volume")
{
    VWAP v;

    // Trades: (price, vol)
    // Step 1: (10, 100)  -> vwap = 10
    // Step 2: (11, 100)  -> vwap = (10*100 + 11*100) / 200 = 10.5
    // Step 3: ( 9,  50)  -> vwap = (1000 + 1100 + 450) / 250 = 10.0
    double v1 = v.update(10.0, 100.0);
    REQUIRE(v1 == Approx(10.0));
    REQUIRE(v.value().has_value());

    double v2 = v.update(11.0, 100.0);
    REQUIRE(v2 == Approx(10.5));

    double v3 = v.update(9.0, 50.0);
    REQUIRE(v3 == Approx(10.2));
}

TEST_CASE("VWAP reset_session clears cumulative state")
{
    VWAP v;
    v.update(10.0, 100.0);
    v.update(12.0, 100.0);
    REQUIRE(v.value().has_value());
    REQUIRE(*v.value() == Approx(11.0));

    v.reset_session();
    REQUIRE_FALSE(v.value().has_value());

    // New session
    auto x = v.update(20.0, 10.0);
    REQUIRE(x == Approx(20.0));
}

TEST_CASE("VWAP OHLC path equals direct TP path")
{
    VWAP v1, v2;

    // Bars: H,L,C,V; TP=(H+L+C)/3
    struct Bar
    {
        double h, l, c, v;
    };
    std::vector<Bar> bars = {
        {11, 9, 10, 100},
        {12, 10, 11, 200},
        {13, 11, 12, 300},
        {14, 12, 13, 400},
    };

    double last1 = 0.0, last2 = 0.0;
    for (auto &b : bars)
    {
        last1 = v1.update(b.h, b.l, b.c, b.v);
        double tp = (b.h + b.l + b.c) / 3.0;
        last2 = v2.update(tp, b.v);
        REQUIRE(last1 == Approx(last2).margin(1e-12));
    }
    REQUIRE(last1 == Approx(last2).margin(1e-12));
}

TEST_CASE("VWAP batch compute matches incremental (price/volume)")
{
    std::vector<double> P = {10, 11, 9, 12, 13};
    std::vector<double> V = {5, 10, 10, 5, 20};

    auto batch = VWAP::compute(P, V);

    VWAP inc;
    std::vector<double> inc_series;
    inc_series.reserve(P.size());
    for (std::size_t i = 0; i < P.size(); ++i)
        inc_series.emplace_back(inc.update(P[i], V[i]));

    REQUIRE(batch.size() == inc_series.size());
    for (std::size_t i = 0; i < batch.size(); ++i)
    {
        REQUIRE(batch[i] == Approx(inc_series[i]).margin(1e-12));
    }
}

TEST_CASE("VWAP handles zero volume gracefully")
{
    VWAP v;
    // First trade zero volume -> VWAP stays neutral (0/ε -> 0). Next trade sets it.
    auto a = v.update(100.0, 0.0);
    REQUIRE(std::isfinite(a));
    auto b = v.update(10.0, 10.0);
    REQUIRE(b == Approx(10.0));
}
