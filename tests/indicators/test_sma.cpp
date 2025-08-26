#include <catch2/catch.hpp>
#include "fin/indicators/SMA.hpp"
#include <vector>
#include <optional>

using fin::indicators::SMA;

TEST_CASE("SWA warms up then yields values")
{
    const std::size_t P = 5;
    SMA sma(P);

    // First P-1 updates should be nullopt
    for (std::size_t i = 0; i < P - 1; ++i)
    {
        auto v = sma.update(1.0);
        REQUIRE_FALSE(v.has_value());
    }

    // P-th update yields a value
    auto v = sma.update(1.0);
    REQUIRE(v.has_value());
    REQUIRE(*v == Approx(1.0));
    REQUIRE(sma.value().has_value());
    REQUIRE(*sma.value() == Approx(1.0));
}

TEST_CASE("SMA warms up then yields values")
{
    const std::size_t P = 5;
    SMA sma(P);

    // First P-1 updates should be nullopt
    for (std::size_t i = 0; i < P - 1; ++i)
    {
        auto v = sma.update(1.0);
        REQUIRE_FALSE(v.has_value());
    }

    // P-th update yields a value
    auto v = sma.update(1.0);
    REQUIRE(v.has_value());
    REQUIRE(*v == Approx(1.0));
    REQUIRE(sma.value().has_value());
    REQUIRE(*sma.value() == Approx(1.0));
}

TEST_CASE("SMA compute() equals incremental update()")
{
    const std::size_t P = 4;
    std::vector<double> x = {10, 11, 10, 12, 13, 12, 14, 13};

    auto batch = SMA::compute(x, P);
    SMA inc(P);
    std::vector<std::optional<double>> inc_series;
    for (double xi : x)
        inc_series.emplace_back(inc.update(xi));

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

TEST_CASE("SMA sliding window behaves on mixed values")
{
    const std::size_t P = 3;
    SMA sma(P);

    REQUIRE_FALSE(sma.update(5.0).has_value()); // [5]
    REQUIRE_FALSE(sma.update(7.0).has_value()); // [5,7]
    auto v1 = sma.update(9.0);                  // [5,7,9] -> 7.0
    REQUIRE(v1.has_value());
    REQUIRE(*v1 == Approx((5 + 7 + 9) / 3.0));

    auto v2 = sma.update(-1.0); // [7,9,-1] -> 5.0
    REQUIRE(v2.has_value());
    REQUIRE(*v2 == Approx((7 + 9 - 1) / 3.0));

    auto v3 = sma.update(10.0); // [9,-1,10] -> 6.0
    REQUIRE(v3.has_value());
    REQUIRE(*v3 == Approx((9 - 1 + 10) / 3.0));
}

TEST_CASE("SMA reset clears state")
{
    SMA sma(3);
    sma.update(1.0);
    sma.update(2.0);
    sma.update(3.0);
    REQUIRE(sma.value().has_value());

    sma.reset();
    REQUIRE_FALSE(sma.value().has_value());
    REQUIRE_FALSE(sma.update(4.0).has_value());
    REQUIRE_FALSE(sma.update(5.0).has_value());

    auto v = sma.update(6.0);
    REQUIRE(v.has_value());
    REQUIRE(*v == Approx((4 + 5 + 6) / 3.0));
}
