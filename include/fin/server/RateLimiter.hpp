#pragma once
#ifndef FIN_SERVER_RATE_LIMITER_HPP
#define FIN_SERVER_RATE_LIMITER_HPP

#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

namespace fin::server
{
    /**
     * @brief Fixed-window request limiter, keyed by an opaque identity (an API key or a client
     * address).
     *
     * Each identity gets `max_requests` calls per `window`. The window is not sliding: the first
     * allowed call starts a window, and the counter resets the moment `window` has elapsed since
     * that start. Fixed windows are coarse at the boundary (a client can spend one window's quota
     * at its end and the next at the start), which is a known and acceptable trade for an MVP; a
     * token bucket would smooth it.
     *
     * `now` is passed in rather than read from a clock, so the behaviour is deterministic and the
     * tests need no sleeps. The server passes std::chrono::steady_clock::now().
     *
     * All methods are safe to call from several threads: the server serves each request on its
     * own thread, and they share one limiter.
     */
    class RateLimiter
    {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        // A default-constructed limiter is disabled and allows everything.
        RateLimiter() = default;
        RateLimiter(std::size_t max_requests, std::chrono::seconds window)
            : max_requests_(max_requests), window_(window)
        {
        }

        [[nodiscard]] bool enabled() const noexcept
        {
            return max_requests_ > 0 && window_.count() > 0;
        }

        [[nodiscard]] std::chrono::seconds window() const noexcept { return window_; }
        [[nodiscard]] std::size_t max_requests() const noexcept { return max_requests_; }

        // True if a call from `identity` is allowed at `now`, counting it against the quota.
        bool allow(const std::string &identity, TimePoint now)
        {
            if (!enabled())
                return true;

            std::lock_guard<std::mutex> lock(mutex_);

            // Keep the map from growing without bound when many distinct identities appear: drop
            // the ones whose window has already elapsed before inserting a new one.
            if (buckets_.size() >= kMaxTracked)
                prune(now);

            Bucket &bucket = buckets_[identity];
            if (bucket.count == 0 || now - bucket.window_start >= window_)
            {
                bucket.window_start = now;
                bucket.count = 1;
                return true;
            }
            if (bucket.count < max_requests_)
            {
                ++bucket.count;
                return true;
            }
            return false;
        }

    private:
        struct Bucket
        {
            TimePoint window_start{};
            std::size_t count = 0;
        };

        void prune(TimePoint now)
        {
            for (auto it = buckets_.begin(); it != buckets_.end();)
            {
                if (now - it->second.window_start >= window_)
                    it = buckets_.erase(it);
                else
                    ++it;
            }
        }

        static constexpr std::size_t kMaxTracked = 100000;

        std::size_t max_requests_ = 0;
        std::chrono::seconds window_{0};
        std::mutex mutex_;
        std::unordered_map<std::string, Bucket> buckets_;
    };

} // namespace fin::server

#endif // FIN_SERVER_RATE_LIMITER_HPP
