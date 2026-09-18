#include "catch2_compat.hpp"

#include <chrono>
#include <vector>

#include "fin/core/Tick.hpp"
#include "fin/io/MockTickSource.hpp"
#include "fin/io/Sources.hpp"

namespace
{
    fin::core::Tick make_tick(long long ms, double price, const std::string &symbol = "ABC")
    {
        using namespace std::chrono;
        const fin::core::Timestamp ts{duration_cast<nanoseconds>(milliseconds{ms})};
        return fin::core::Tick(ts, fin::core::Symbol(symbol),
                               fin::core::Price(price), fin::core::Volume(1.0));
    }

    std::vector<fin::core::Tick> three_ticks()
    {
        return {make_tick(1000, 100.0), make_tick(2000, 101.0), make_tick(3000, 102.5)};
    }
}

TEST_CASE("MockTickSource replays its ticks in order and stays exhausted", "[io][stream]")
{
    fin::io::MockTickSource source(three_ticks());
    REQUIRE(source.size() == 3);
    REQUIRE(source.remaining() == 3);

    auto first = source.next();
    REQUIRE(first.has_value());
    REQUIRE(first->price().value() == Approx(100.0).margin(1e-12));

    auto second = source.next();
    REQUIRE(second.has_value());
    REQUIRE(second->price().value() == Approx(101.0).margin(1e-12));

    auto third = source.next();
    REQUIRE(third.has_value());
    REQUIRE(third->price().value() == Approx(102.5).margin(1e-12));
    REQUIRE(source.remaining() == 0);

    // Exhausted, and it stays that way however often it is asked again — a driver loop that
    // calls next() once more after EOF must not wrap around to the start.
    REQUIRE_FALSE(source.next().has_value());
    REQUIRE_FALSE(source.next().has_value());
}

TEST_CASE("MockTickSource rewinds for a second pass", "[io][stream]")
{
    fin::io::MockTickSource source(three_ticks());

    double first_pass = 0.0;
    while (auto tick = source.next())
        first_pass += tick->price().value();

    source.rewind();
    REQUIRE(source.remaining() == 3);

    double second_pass = 0.0;
    while (auto tick = source.next())
        second_pass += tick->price().value();

    REQUIRE(second_pass == Approx(first_pass).margin(1e-12));
}

TEST_CASE("MockTickSource drives anything taking an ITickSource", "[io][stream]")
{
    // The point of the alias: the streaming engine takes the interface, so a test can hand it
    // a vector of ticks where production hands it a file.
    fin::io::MockTickSource concrete(three_ticks());
    fin::io::ITickSource &source = concrete;

    std::size_t count = 0;
    long long last_ms = 0;
    while (auto tick = source.next())
    {
        using namespace std::chrono;
        const long long ms =
            duration_cast<milliseconds>(tick->timestamp().time_since_epoch()).count();
        REQUIRE(ms > last_ms); // arrives in order
        last_ms = ms;
        ++count;
    }

    REQUIRE(count == 3);
    REQUIRE(last_ms == 3000);
}
