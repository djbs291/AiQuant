#include <catch2/catch.hpp>
#include "fin/core/Volume.hpp"

using namespace fin::core;

TEST_CASE("Volume operations", "[Volume]")
{
    Volume v1(10.0);
    Volume v2(15.0);

    REQUIRE((v1 + v2).value() == 25.0);
    REQUIRE(v1.value() == 10.0);
}
