#include <catch2/catch.hpp>
#include "fin/indicators/BollingerBands.hpp"
#include <vector>
#include <optional>

using fin::indicators::BollingerBands;

TEST_CASE("BollingerBands warms up then yields bands")
{
    const std::size_t P = 5;
    BollingerBands bb(P, 2.0);

    // First P-1 updates -> nullopt
    for (std::size_t i = 0; i < P - 1; ++i)
    {
        auto v = bb.update(100.0);
        REQUIRE_FALSE(v.has_value());
    }

    // P-th update -> bands exist
    auto bands = bb.update(100.0);
    REQUIRE(bands.has_value());
    REQUIRE(bands->middle == Approx(100.0));
    REQUIRE(bands->upper == Approx(100.0));
    REQUIRE(bands->lower == Approx(100.0));
}

TEST_CASE("BollingerBands variance works")
{
    const std::size_t P = 3;
    BollingerBands bb(P, 2.0);

    std::vector<double> x = {1, 2, 3, 4, 5};
    std::vector<std::optional<BollingerBands::Bands>> out;
    for (double xi : x)
        out.push_back(bb.update(xi));

    // From step 3 onward, should have bands
    REQUIRE_FALSE(out[0].has_value());
    REQUIRE_FALSE(out[1].has_value());
    REQUIRE(out[2].has_value());

    auto bands = *out[2];
    REQUIRE(bands.middle == Approx((1 + 2 + 3) / 3.0));

    // Check stddev manually for {2,3,4}
    auto bands2 = *out[3];
    double mean = (2 + 3 + 4) / 3.0;
    double var = ((2 * 2 + 3 * 3 + 4 * 4) / 3.0) - mean * mean;
    double stdev = std::sqrt(var);
    REQUIRE(bands2.middle == Approx(mean));
    REQUIRE(bands2.upper == Approx(mean + 2.0 * stdev));
    REQUIRE(bands2.lower == Approx(mean - 2.0 * stdev));
}

TEST_CASE("Batch compute matches incremental")
{
    const std::size_t P = 4;
    std::vector<double> x = {10, 11, 12, 13, 14, 15, 16, 17};

    auto batch = BollingerBands::compute(x, P, 2.0);

    BollingerBands inc(P, 2.0);
    std::vector<std::optional<BollingerBands::Bands>> inc_series;
    for (double xi : x)
        inc_series.emplace_back(inc.update(xi));

    REQUIRE(batch.size() == inc_series.size());
    for (std::size_t i = 0; i < batch.size(); ++i)
    {
        if (batch[i].has_value() || inc_series[i].has_value())
        {
            REQUIRE(batch[i].has_value());
            REQUIRE(inc_series[i].has_value());
            REQUIRE(batch[i]->middle == Approx(inc_series[i]->middle).margin(1e-12));
            REQUIRE(batch[i]->upper == Approx(inc_series[i]->upper).margin(1e-12));
            REQUIRE(batch[i]->lower == Approx(inc_series[i]->lower).margin(1e-12));
        }
    }
}