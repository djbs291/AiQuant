#include "catch2_compat.hpp"
#include "fin/indicators/Stochastic.hpp"
#include <vector>
#include <optional>

using fin::indicators::Stochastic;
using fin::indicators::StochOut;

TEST_CASE("Stochastic warmup then yields K and D")
{
    const std::size_t K = 5, D = 3;
    Stochastic st(K, D);

    // Build a simple sequence
    std::vector<double> H = {10, 11, 12, 13, 14, 15, 16};
    std::vector<double> L = {9, 10, 11, 12, 13, 14, 15};
    std::vector<double> C = {9.5, 10.5, 11.5, 12.5, 13.5, 14.5, 15.5};

    // Until kPeriod, no %K; until dPeriod %K values, no %D
    std::vector<std::optional<StochOut>> out;
    for (std::size_t i = 0; i < H.size(); ++i)
    {
        out.emplace_back(st.update(H[i], L[i], C[i]));
    }

    // First possible output index = (K-1) + (D-1) = K + D - 2
    std::size_t first_idx = K + D - 2;
    for (std::size_t i = 0; i < first_idx; ++i)
    {
        REQUIRE_FALSE(out[i].has_value());
    }
    REQUIRE(out[first_idx].has_value());
    REQUIRE(out[first_idx]->k >= 0.0);
    REQUIRE(out[first_idx]->k <= 100.0);
    REQUIRE(out[first_idx]->d >= 0.0);
    REQUIRE(out[first_idx]->d <= 100.0);
}

TEST_CASE("Stochastic flat market gives neutral 50 for K and D")
{
    const std::size_t K = 3, D = 3;
    Stochastic st(K, D);

    std::vector<double> H = {10, 10, 10, 10, 10, 10};
    std::vector<double> L = {10, 10, 10, 10, 10, 10};
    std::vector<double> C = {10, 10, 10, 10, 10, 10};

    std::optional<StochOut> last;
    for (std::size_t i = 0; i < H.size(); ++i)
    {
        last = st.update(H[i], L[i], C[i]);
    }

    REQUIRE(last.has_value());
    REQUIRE(last->k == Approx(50.0));
    REQUIRE(last->d == Approx(50.0));
}

TEST_CASE("Stochastic uptrend tends to high K values after warmup")
{
    const std::size_t K = 5, D = 3;
    Stochastic st(K, D);

    // Gradual uptrend
    std::vector<double> H, L, C;
    for (int i = 0; i < 60; ++i)
    {
        double base = 100.0 + i * 0.5;
        L.push_back(base - 0.5);
        H.push_back(base + 0.5);
        C.push_back(base);
    }

    std::size_t cnt = 0, highK = 0;
    for (std::size_t i = 0; i < H.size(); ++i)
    {
        auto o = st.update(H[i], L[i], C[i]);
        if (o.has_value())
        {
            ++cnt;
            if (o->k > 80.0)
                ++highK;
        }
    }
    REQUIRE(cnt > 20);
    REQUIRE(highK >= cnt * 0.6);
}

TEST_CASE("Batch compute equals incremental")
{
    const std::size_t K = 7, D = 3;
    std::vector<double> H, L, C;
    for (int i = 0; i < 50; ++i)
    {
        double base = 50.0 + std::sin(i * 0.2) * 5.0;
        L.push_back(base - 1.0);
        H.push_back(base + 1.0);
        C.push_back(base);
    }

    auto batch = Stochastic::compute(H, L, C, K, D);
    Stochastic inc(K, D);
    std::vector<std::optional<StochOut>> inc_series;
    for (std::size_t i = 0; i < H.size(); ++i)
        inc_series.emplace_back(inc.update(H[i], L[i], C[i]));

    REQUIRE(batch.size() == inc_series.size());
    for (std::size_t i = 0; i < batch.size(); ++i)
    {
        if (batch[i].has_value() || inc_series[i].has_value())
        {
            REQUIRE(batch[i].has_value());
            REQUIRE(inc_series[i].has_value());
            REQUIRE(batch[i]->k == Approx(inc_series[i]->k).margin(1e-12));
            REQUIRE(batch[i]->d == Approx(inc_series[i]->d).margin(1e-12));
        }
    }
}

TEST_CASE("Reset clears state and restarts warmup")
{
    Stochastic st(3, 3);

    // Produce enough bars to get %D:
    st.update(11, 10, 10.5); // 0: no K
    st.update(12, 11, 11.5); // 1: no K
    st.update(13, 12, 12.5); // 2: K #1
    st.update(14, 13, 13.5); // 3: K #2
    st.update(15, 14, 14.5); // 4: K #3 -> first D available here

    REQUIRE(st.value().has_value()); // now true

    st.reset();
    REQUIRE_FALSE(st.value().has_value());

    // After reset, warmup restarts (need 5 bars again for 3,3)
    REQUIRE_FALSE(st.update(10, 9, 9.5).has_value());
    REQUIRE_FALSE(st.update(11, 10, 10.5).has_value());
    REQUIRE_FALSE(st.update(12, 11, 11.5).has_value()); // first K
    REQUIRE_FALSE(st.update(13, 12, 12.5).has_value()); // second K
    auto v = st.update(14, 13, 13.5);                   // third K -> first D
    REQUIRE(v.has_value());
}
