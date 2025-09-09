#include "catch2_compat.hpp"
#include "fin/indicators/ATR.hpp"
#include <vector>
#include <optional>
#include <cmath>

using fin::indicators::ATR;

TEST_CASE("ATR warmup then yields values")
{
    const std::size_t P = 3;
    ATR atr(P);

    // First bar: no TR (no prev_close yet)
    REQUIRE_FALSE(atr.update(10, 9, 9.5).has_value());

    // Next P bars accumulate TR and on the P-th TR we get first ATR
    REQUIRE_FALSE(atr.update(11, 9.4, 10.5).has_value());  // TR #1
    REQUIRE_FALSE(atr.update(12, 10.0, 11.0).has_value()); // TR #2
    auto v = atr.update(12.5, 11.5, 12.0);                 // TR #3 -> first ATR
    REQUIRE(v.has_value());
}

TEST_CASE("ATR seeding equals average of first period TRs")
{
    // Construct small OHLC set to compute TR by hand
    const std::size_t P = 3;
    ATR atr(P);

    // Bar 0: seed prev_close
    REQUIRE_FALSE(atr.update(10, 9, 9.0).has_value()); // prev_close=9.0

    // Bar 1:
    // hl=1.5, |h-prevC|=|10.5-9|=1.5, |l-prevC|=|9.0-9|=0 -> TR1=1.5
    REQUIRE_FALSE(atr.update(10.5, 9.0, 9.5).has_value());

    // Bar 2:
    // hl=2.0, |h-prevC|=|11.0-9.5|=1.5, |l-prevC|=|9.0-9.5|=0.5 -> TR2=2.0
    REQUIRE_FALSE(atr.update(11.0, 9.0, 10.0).has_value());

    // Bar 3:
    // hl=1.0, |h-prevC|=|10.5-10.0|=0.5, |l-prevC|=|9.5-10.0|=0.5 -> TR3=1.0
    auto first = atr.update(10.5, 9.5, 10.0);
    REQUIRE(first.has_value());
    REQUIRE(*first == Approx((1.5 + 2.0 + 1.0) / 3.0));
}

TEST_CASE("ATR Wilder update matches recurrence")
{
    const std::size_t P = 3;
    ATR atr(P);

    // Seed prev_close
    atr.update(10, 9, 9.5); // no value

    // Accumulate TR1..TR3
    // We'll hand-compute TRs to compare
    auto tr = [&](double h, double l, double pc)
    { return std::max({h - l, std::fabs(h - pc), std::fabs(l - pc)}); };

    double prev_close = 9.5;
    double h1 = 11, l1 = 10, c1 = 10.5;
    double tr1 = tr(h1, l1, prev_close);
    prev_close = c1;
    atr.update(h1, l1, c1);
    double h2 = 12, l2 = 10, c2 = 11.0;
    double tr2 = tr(h2, l2, prev_close);
    prev_close = c2;
    atr.update(h2, l2, c2);
    double h3 = 12, l3 = 11, c3 = 11.5;
    double tr3 = tr(h3, l3, prev_close);
    prev_close = c3;
    auto first = atr.update(h3, l3, c3); // first ATR
    REQUIRE(first.has_value());
    double atr_seed = (tr1 + tr2 + tr3) / 3.0;
    REQUIRE(*first == Approx(atr_seed));

    // Next TR and Wilder smoothing
    double h4 = 13, l4 = 11.5, c4 = 12.5;
    double tr4 = tr(h4, l4, prev_close);
    auto next = atr.update(h4, l4, c4);
    REQUIRE(next.has_value());
    double expected = (atr_seed * (P - 1) + tr4) / P;
    REQUIRE(*next == Approx(expected));
}

TEST_CASE("ATR batch equals incremental")
{
    const std::size_t P = 5;
    std::vector<double> H = {10, 11, 12, 13, 14, 15, 16, 15, 14, 13};
    std::vector<double> L = {9, 10, 11, 12, 13, 14, 15, 14, 13, 12};
    std::vector<double> C = {9.5, 10.5, 11.5, 12.5, 13.5, 14.5, 15.5, 14.5, 13.5, 12.5};

    auto batch = ATR::compute(H, L, C, P);

    ATR inc(P);
    std::vector<std::optional<double>> inc_series;
    for (std::size_t i = 0; i < H.size(); ++i)
        inc_series.emplace_back(inc.update(H[i], L[i], C[i]));

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

TEST_CASE("ATR reset clears state")
{
    ATR atr(3);
    atr.update(10, 9, 9.5);
    atr.update(11, 9.5, 10.0);
    atr.update(12, 10.0, 11.0);
    atr.update(12.5, 11.5, 12.0);
    REQUIRE(atr.value().has_value());

    atr.reset();
    REQUIRE_FALSE(atr.value().has_value());
    REQUIRE_FALSE(atr.update(10, 9, 9.5).has_value());
}
