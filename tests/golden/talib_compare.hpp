#pragma once

// Shared scaffolding for the golden tests that check our indicators against TA-Lib.
// This header is only ever compiled into aiquant_talib_tests, which always links real
// Catch2 v3 and the TA-Lib C library, so it can use v3-only macros such as INFO.

#include "catch2_compat.hpp"

#include <ta-lib/ta_libc.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <vector>

namespace golden
{
    // TA-Lib keeps global state and wants one TA_Initialize() per process.
    inline void ensure_ta_initialized()
    {
        static const bool ok = [] {
            if (TA_Initialize() != TA_SUCCESS)
                return false;
            std::atexit([] { TA_Shutdown(); });
            return true;
        }();
        REQUIRE(ok);
    }

    struct Series
    {
        std::vector<double> open;
        std::vector<double> high;
        std::vector<double> low;
        std::vector<double> close;
    };

    // Deterministic OHLC: a gentle trend plus two sine components of different periods.
    // The spread is never zero, so no high/low window is ever flat — that is the one case
    // where our Stochastic (50.0) and TA-Lib (0.0) disagree by design.
    inline Series make_series(std::size_t n = 500)
    {
        Series s;
        s.open.reserve(n);
        s.high.reserve(n);
        s.low.reserve(n);
        s.close.reserve(n);

        double previous_close = 100.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double>(i);
            const double base = 100.0 + 0.05 * t + 8.0 * std::sin(t / 11.0) + 3.0 * std::sin(t / 3.0);
            const double spread = 0.5 + 0.25 * std::fabs(std::sin(t / 7.0)) + 0.05 * static_cast<double>(i % 5);

            s.open.push_back(previous_close);
            s.close.push_back(base);
            s.high.push_back(std::max(base, previous_close) + spread);
            s.low.push_back(std::min(base, previous_close) - spread);
            previous_close = base;
        }
        return s;
    }

    inline std::size_t first_ready(const std::vector<std::optional<double>> &values)
    {
        for (std::size_t i = 0; i < values.size(); ++i)
            if (values[i].has_value())
                return i;
        return values.size();
    }

    // Flattens a vector of optional composite outputs into one scalar series, keeping the
    // per-bar indexing (nullopt stays nullopt).
    template <typename T, typename Projection>
    std::vector<std::optional<double>> project(const std::vector<std::optional<T>> &values, Projection get)
    {
        std::vector<std::optional<double>> out;
        out.reserve(values.size());
        for (const auto &value : values)
            out.push_back(value ? std::optional<double>(get(*value)) : std::nullopt);
        return out;
    }

    // Compares our per-bar series against a TA-Lib output block.
    //
    // TA-Lib writes a compacted array: ta_out[0] is the value for bar out_beg_idx, which
    // equals that function's lookback. Ours is indexed by bar, with nullopt during warmup.
    //
    //   expect_first_bar    if set, assert our first value lands on exactly that bar
    //   compare_from_bar    start comparing at this bar (for asymptotic cases such as MACD)
    inline void compare_against_talib(const char *label,
                                      const std::vector<std::optional<double>> &ours,
                                      const std::vector<double> &ta_out,
                                      int out_beg_idx,
                                      int out_nb_element,
                                      double tolerance,
                                      std::optional<std::size_t> expect_first_bar = std::nullopt,
                                      std::size_t compare_from_bar = 0)
    {
        INFO("indicator: " << label);
        REQUIRE(out_nb_element > 0);

        const std::size_t ours_first = first_ready(ours);
        {
            INFO("our first value is at bar " << ours_first << ", TA-Lib's at bar " << out_beg_idx);
            REQUIRE(ours_first < ours.size());
            if (expect_first_bar)
                REQUIRE(ours_first == *expect_first_bar);
        }

        const std::size_t begin = std::max({compare_from_bar,
                                            static_cast<std::size_t>(out_beg_idx),
                                            ours_first});
        const std::size_t end = static_cast<std::size_t>(out_beg_idx) +
                                static_cast<std::size_t>(out_nb_element);
        REQUIRE(begin < end);

        std::size_t compared = 0;
        for (std::size_t bar = begin; bar < end && bar < ours.size(); ++bar)
        {
            const double theirs = ta_out[bar - static_cast<std::size_t>(out_beg_idx)];
            INFO("bar " << bar << " of [" << begin << ", " << end << ")");
            REQUIRE(ours[bar].has_value());

            const double mine = *ours[bar];
            const double absolute = std::fabs(mine - theirs);
            const double relative = absolute / std::max(1.0, std::fabs(theirs));
            INFO("ours=" << mine << " talib=" << theirs << " abs=" << absolute << " rel=" << relative);
            REQUIRE(relative <= tolerance);
            ++compared;
        }

        INFO("compared " << compared << " bars");
        REQUIRE(compared > 0);
    }
}
