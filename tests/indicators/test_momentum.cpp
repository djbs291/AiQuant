#include "catch2_compat.hpp"
#include "fin/indicators/Momentum.hpp"
#include <vector>

using fin::indicators::Momentum;

TEST_CASE("Momentum warmup and difference mode")
{
    Momentum m(3, Momentum::Mode::Difference);
    REQUIRE_FALSE(m.update(10).has_value());
    REQUIRE_FALSE(m.update(11).has_value());
    REQUIRE_FALSE(m.update(12).has_value()); // now have baseline
    auto v = m.update(15);                   // diff = 15 - 10 = 5
    REQUIRE(v.has_value());
    REQUIRE(*v == Approx(5.0));
}

TEST_CASE("Momentum rate mode (ROC)")
{
    Momentum m(2, Momentum::Mode::Rate);
    m.update(10);          // warmup
    m.update(12);          // warmup complete; period=2 => baseline is 2 bars ago (10)
    auto v = m.update(15); // prev=10 -> (15/10) - 1 = 0.5
    REQUIRE(v.has_value());
    REQUIRE(*v == Approx(0.5));
}

TEST_CASE("Momentum batch equals incremental")
{
    std::vector<double> x{10, 12, 15, 18, 21, 24};
    auto batch = Momentum::compute(x, 3, Momentum::Mode::Difference);
    Momentum inc(3, Momentum::Mode::Difference);
    std::vector<std::optional<double>> inc_series;
    for (double c : x)
        inc_series.emplace_back(inc.update(c));
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
