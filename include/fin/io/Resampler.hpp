#pragma once
#ifndef FIN_IO_RESAMPLER_HPP
#define FIN_IO_RESAMPLER_HPP

#include <optional>
#include <chrono>

#include "fin/core/Tick.hpp"
#include "fin/core/Candle.hpp"
#include "fin/core/Timestamp.hpp"
#include "fin/core/Price.hpp"
#include "fin/core/Volume.hpp"

namespace fin::io
{
    class Resampler
    {
    public:
        using Duration = std::chrono::milliseconds;

        explicit Resampler(Duration interval) : interval_(interval) {}

        // Feed a tick; emits a finished Candle when the time bucket rolls
        std::optional<fin::core::Candle> update(const fin::core::Tick &t);

        // Close the current partial candle (if any).
        std::optional<fin::core::Candle> flush();

        Duration interval() const noexcept { return interval_; }

    private:
        Duration interval_;
        bool have_open_ = false;

        fin::core::Timestamp window_start_{};
        fin::core::Price open_{0.0};
        fin::core::Price high_{0.0};
        fin::core::Price low_{0.0};
        fin::core::Price close_{0.0};
        fin::core::Volume vol_{0.0};

        fin::core::Timestamp floor_ts(fin::core::Timestamp ts) const;
        fin::core::Candle make_candle() const;
        void open_new(fin::core::Timestamp start, const fin::core::Tick &t);
        void accumulate(const fin::core::Tick &t);
    };

} // namespace fin::io

#endif // FIN_IO_RESAMPLER_HPP
