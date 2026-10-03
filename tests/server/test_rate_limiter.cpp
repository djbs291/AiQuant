#include "catch2_compat.hpp"

#include <chrono>

#include "fin/server/RateLimiter.hpp"

using fin::server::RateLimiter;
using namespace std::chrono_literals;

namespace
{
    RateLimiter::TimePoint base()
    {
        return RateLimiter::TimePoint{}; // a fixed epoch; the limiter only uses differences
    }
}

TEST_CASE("A default RateLimiter is disabled and allows everything", "[server][ratelimit]")
{
    RateLimiter limiter;
    REQUIRE_FALSE(limiter.enabled());
    for (int i = 0; i < 1000; ++i)
        REQUIRE(limiter.allow("anyone", base()));
}

TEST_CASE("A zero window or zero quota leaves the limiter disabled", "[server][ratelimit]")
{
    REQUIRE_FALSE(RateLimiter(0, 60s).enabled());
    REQUIRE_FALSE(RateLimiter(10, 0s).enabled());
}

TEST_CASE("RateLimiter allows up to the quota, then refuses within the window", "[server][ratelimit]")
{
    RateLimiter limiter(3, 60s);
    REQUIRE(limiter.enabled());

    const auto t0 = base();
    REQUIRE(limiter.allow("k", t0));
    REQUIRE(limiter.allow("k", t0 + 1s));
    REQUIRE(limiter.allow("k", t0 + 2s));
    // Fourth call inside the same window is refused.
    REQUIRE_FALSE(limiter.allow("k", t0 + 3s));
    REQUIRE_FALSE(limiter.allow("k", t0 + 59s));
}

TEST_CASE("RateLimiter resets once the window has elapsed", "[server][ratelimit]")
{
    RateLimiter limiter(2, 60s);
    const auto t0 = base();
    REQUIRE(limiter.allow("k", t0));
    REQUIRE(limiter.allow("k", t0 + 10s));
    REQUIRE_FALSE(limiter.allow("k", t0 + 20s));

    // At exactly the window boundary the counter resets.
    REQUIRE(limiter.allow("k", t0 + 60s));
    REQUIRE(limiter.allow("k", t0 + 61s));
    REQUIRE_FALSE(limiter.allow("k", t0 + 62s));
}

TEST_CASE("RateLimiter counts each identity independently", "[server][ratelimit]")
{
    RateLimiter limiter(1, 60s);
    const auto t0 = base();
    REQUIRE(limiter.allow("alice", t0));
    REQUIRE(limiter.allow("bob", t0));
    // Each has spent its single call.
    REQUIRE_FALSE(limiter.allow("alice", t0 + 1s));
    REQUIRE_FALSE(limiter.allow("bob", t0 + 1s));
}
