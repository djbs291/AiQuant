#include "catch2_compat.hpp"
#include "fin/core/Tick.hpp"

using namespace fin::core;

TEST_CASE("Tick holds correct data", "[Tick]")
{
    Timestamp ts = std::chrono::system_clock::now();
    Symbol sym("AAPL");
    Price price(150.5);
    Volume vol(200);

    Tick tick(ts, sym, price, vol);

    REQUIRE(tick.timestamp() == ts);
    REQUIRE(tick.symbol() == sym);
    REQUIRE(tick.price() == price);
    REQUIRE(tick.volume().value() == 200);
}
