#include "catch2_compat.hpp"
#include "fin/indicators/RSI.hpp"
#include "fin/core/Price.hpp"

using namespace fin::core;
using namespace fin::indicators;

TEST_CASE("RSI basic behavior", "[RSI]")
{
    RSI rsi(14);
    double prices[] = {
        44.34, 44.09, 44.15, 43.61, 44.33, 44.83, 45.10,
        45.42, 45.84, 46.08, 45.89, 46.03, 45.61, 46.28,
        46.28, 46.00, 46.03, 46.41, 46.22, 45.64, 46.21};

    for (size_t i = 0; i < sizeof(prices) / sizeof(double); ++i)
    {
        rsi.update(Price(prices[i]));
    }

    REQUIRE(rsi.is_ready());
    double rsi_val = rsi.value();
    REQUIRE(rsi_val > 0.0);
    REQUIRE(rsi_val < 100.0);
}
