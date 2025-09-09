#include "catch2_compat.hpp"
#include "fin/indicators/ADX.hpp"
#include <vector>
#include <optional>
#include <cmath>

using fin::indicators::ADX;
using fin::indicators::ADXOut;

static std::size_t first_adx_index(std::size_t period)
{
    // bar 0 seeds prev; first ADX after:
    // - N TR/DM values (bars 1..N) => first DI/DX at bar N
    // - N DX values (bars N..2N-1) => first ADX at bar 2N-1
    return 2 * period - 1;
}

TEST_CASE("ADX warmup timeline respected")
{
    const std::size_t P = 14;
    ADX adx(P);

    // Feed a long flat-ish sequence just to drive warmup
    for (std::size_t i = 0; i < first_adx_index(P); ++i)
    {
        auto v = adx.update(100 + i * 0.1, 100 + i * 0.1 - 1.0, 100 + i * 0.1 - 0.4);
        REQUIRE_FALSE(v.has_value());
    }
    auto first = adx.update(102.0, 101.0, 101.5);
    REQUIRE(first.has_value());
    REQUIRE(std::isfinite(first->plusDI));
    REQUIRE(std::isfinite(first->minusDI));
    REQUIRE(std::isfinite(first->dx));
    REQUIRE(std::isfinite(first->adx));
}

TEST_CASE("ADX batch equals incremental")
{
    const std::size_t P = 10;
    std::vector<double> H, L, C;
    for (int i = 0; i < 80; ++i)
    {
        double base = 100.0 + std::sin(i * 0.15) * 2.0;
        H.push_back(base + 1.0);
        L.push_back(base - 1.0);
        C.push_back(base);
    }

    auto batch = ADX::compute(H, L, C, P);

    ADX inc(P);
    std::vector<std::optional<ADXOut>> inc_series;
    inc_series.reserve(H.size());
    for (std::size_t i = 0; i < H.size(); ++i)
        inc_series.emplace_back(inc.update(H[i], L[i], C[i]));

    REQUIRE(batch.size() == inc_series.size());
    for (std::size_t i = 0; i < batch.size(); ++i)
    {
        if (batch[i].has_value() || inc_series[i].has_value())
        {
            REQUIRE(batch[i].has_value());
            REQUIRE(inc_series[i].has_value());
            REQUIRE(batch[i]->plusDI == Approx(inc_series[i]->plusDI).margin(1e-9));
            REQUIRE(batch[i]->minusDI == Approx(inc_series[i]->minusDI).margin(1e-9));
            REQUIRE(batch[i]->dx == Approx(inc_series[i]->dx).margin(1e-9));
            REQUIRE(batch[i]->adx == Approx(inc_series[i]->adx).margin(1e-9));
        }
    }
}

TEST_CASE("ADX is higher in directional wide trend than in choppy oscillation")
{
    using fin::indicators::ADX;
    using fin::indicators::ADXOut;

    const std::size_t P = 7;
    ADX adx(P);

    auto feed = [&](double h, double l, double c)
    {
        return adx.update(h, l, c);
    };

    // Warm up exactly to first ADX availability (bar index 2P-1)
    const std::size_t first_adx_idx = 2 * P - 1;
    for (std::size_t i = 0; i < first_adx_idx; ++i)
    {
        feed(100.0, 99.5, 99.75);
    }

    // -------- Phase A: CHOPPY (alternating direction, narrow range)
    // High/Low alternate around a base so +DM/-DM alternate & cancel over smoothing.
    int a_count = 0;
    double a_sum = 0.0;
    double prev_base = 100.0;
    for (int i = 0; i < 80; ++i)
    {
        // alternate +/− steps to force +DM and −DM to share dominance
        double step = (i % 2 == 0 ? +0.35 : -0.35);
        double base = prev_base + step;
        double h = base + 0.20;
        double l = base - 0.20;
        double c = base; // mid-close
        auto out = feed(h, l, c);
        if (out.has_value())
        {
            ++a_count;
            a_sum += out->adx;
        }
        prev_base = base;
    }
    REQUIRE(a_count > 20);
    double a_avg = a_sum / a_count;

    // -------- Phase B: TRENDING (directional, wider range)
    int b_count = 0;
    double b_sum = 0.0;
    double base = prev_base;
    for (int i = 0; i < 80; ++i)
    {
        base += 0.70;           // strong upward drift
        double h = base + 1.80; // wide, directional range
        double l = base - 0.20;
        double c = base + 0.90; // close skewed upward
        auto out = feed(h, l, c);
        if (out.has_value())
        {
            ++b_count;
            b_sum += out->adx;
        }
    }
    REQUIRE(b_count > 20);
    double b_avg = b_sum / b_count;

    // Robust comparisons: directional wide trend should lift ADX clearly above choppy phase
    REQUIRE(b_avg > a_avg + 10.0);
    REQUIRE(b_avg > 20.0);
}

TEST_CASE("ADX Reset clears state and restarts warmup")
{
    ADX adx(5);
    // drive to ADX availability
    for (int i = 0; i < 11; ++i)
        adx.update(10 + i, 9 + i, 9.5 + i);
    auto v1 = adx.update(21, 19.5, 20.1);
    REQUIRE(v1.has_value());

    adx.reset();
    REQUIRE_FALSE(adx.update(10, 9.5, 9.8).has_value());   // prev set
    REQUIRE_FALSE(adx.update(11, 10.2, 10.6).has_value()); // start seeding again
}
