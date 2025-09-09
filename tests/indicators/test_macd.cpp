#include "catch2_compat.hpp"
#include "fin/indicators/MACD.hpp"
#include <vector>
#include <optional>
#include <algorithm>
#include <cmath>

using fin::indicators::MACD;
using fin::indicators::MACDValue;

static std::size_t warmup_len(std::size_t fast, std::size_t slow, std::size_t signal)
{
    // 0-based first outputs:
    // fast EMA at fast-1; slow EMA at slow-1; first MACD at max(fast,slow)-1
    // signal EMA on its signal-th MACD sample: + (signal-1)
    // => first full MACD set at: max(fast,slow) + signal - 2
    return (std::max(fast, slow) + signal - 2);
}

TEST_CASE("MACD warmup semantics")
{
    const std::size_t FAST = 12, SLOW = 26, SIG = 9;
    MACD m(FAST, SLOW, SIG);

    const std::size_t need = warmup_len(FAST, SLOW, SIG);
    for (std::size_t i = 0; i < need; ++i)
    {
        auto v = m.update(100.0 + i);
        REQUIRE_FALSE(v.has_value()); // still warming up
    }

    // Next tick should produce first full MACD set
    auto v = m.update(100.0 + need);
    REQUIRE(v.has_value());
    REQUIRE(std::isfinite(v->macd));
    REQUIRE(std::isfinite(v->signal));
    REQUIRE(std::isfinite(v->hist));
}

TEST_CASE("MACD sign behavior on trends (use MACD sign, not hist, and pre-seed)")
{
    // Shorter periods => more responsive
    const std::size_t FAST = 6, SLOW = 13, SIG = 4;
    MACD m(FAST, SLOW, SIG);

    // Pre-seed so signal EMA is live before measuring
    const std::size_t need = warmup_len(FAST, SLOW, SIG) + 1;
    for (std::size_t i = 0; i < need; ++i)
    {
        m.update(100.0); // flat to complete warmup
    }

    // Build clear uptrend then downtrend
    std::vector<double> up(40), down(40);
    for (std::size_t i = 0; i < up.size(); ++i)
        up[i] = 100.0 + i * 0.5;
    for (std::size_t i = 0; i < down.size(); ++i)
        down[i] = 120.0 - i * 0.5;

    std::size_t macd_pos = 0, macd_neg = 0, macd_total_up = 0, macd_total_down = 0;

    for (double v : up)
    {
        auto o = m.update(v);
        if (o.has_value())
        {
            ++macd_total_up;
            if (o->macd > 0)
                ++macd_pos;
        }
    }
    for (double v : down)
    {
        auto o = m.update(v);
        if (o.has_value())
        {
            ++macd_total_down;
            if (o->macd < 0)
                ++macd_neg;
        }
    }

    REQUIRE(macd_total_up >= 20);
    REQUIRE(macd_total_down >= 20);

    // In sustained uptrend/downtrend, MACD should reflect sign majority of the time
    REQUIRE(macd_pos >= macd_total_up * 0.6);
    REQUIRE(macd_neg >= macd_total_down * 0.6);
}
