#define CATCH_CONFIG_MAIN

#include <catch2/catch.hpp>
#include "fin/core/Price.hpp"

using namespace fin::core;

TEST_CASE("Price basic operations", "[Price]")
{
    Price p1(100.0);
    Price p2(50.0);

    REQUIRE(p1.value() == 100.0);
    REQUIRE((p1 - p2).value() == 50.0);
    REQUIRE((p1 + p2).value() == 150.0);
    REQUIRE(p1 == Price(100.0));
    REQUIRE(p2 < p1);
}
