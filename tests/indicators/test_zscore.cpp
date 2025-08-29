#include <catch2/catch.hpp>
#include "fin/indicators/ZScore.hpp"
#include <vector>
#include <cmath>

using fin::indicators::ZScore;

TEST_CASE("ZScore warmup and neutrality on flat series")
{
    ZScore z(3);
    // flat -> stddev = 0 -> z = 0 after warmup
    REQUIRE_FALSE(z.update(10).has_value());
    REQUIRE_FALSE(z.update(10).has_value());
    auto v = z.update(10);
    REQUIRE(v.has_value());
    REQUIRE(*v == Approx(0.0));
}

TEST_CASE("ZScore matches manual computation")
{
    ZScore z(3);
    std::vector<double> x = {1, 2, 4, 4}; // window [2,4,4] at last step
    REQUIRE_FALSE(z.update(1).has_value());
    REQUIRE_FALSE(z.update(2).has_value());
    REQUIRE(z.update(4).has_value()); // first z on [1,2,4], but we don't check it
    auto v = z.update(4);             // window [2,4,4]
    REQUIRE(v.has_value());
    double mean = (2 + 4 + 4) / 3.0;
    double var = ((2 * 2 + 4 * 4 + 4 * 4) / 3.0) - mean * mean;
    double sd = std::sqrt(std::max(0.0, var));
    double z3 = (4 - mean) / (sd > 0 ? sd : 1.0);
    REQUIRE(*v == Approx(z3).margin(1e-12));
}

TEST_CASE("ZScore batch equals incremental")
{
    std::vector<double> x{10, 11, 12, 13, 14, 15, 16, 17};
    auto batch = ZScore::compute(x, 4);
    ZScore inc(4);
    std::vector<std::optional<double>> inc_series;
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
