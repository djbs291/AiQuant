#pragma once
#ifndef FIN_IO_RESAMPLER_HPP
#define FIN_IO_RESAMPLER_HPP

#include <optional>
#include <chrono>

#include "fin/io/Options.hpp"
#include "fin/io/Sources.hpp"
#include "fin/core/Tick.hpp"
#include "fin/core/Candle.hpp"

namespace fin::io
{
    class TickToCandleResampler
    {
    public:
        explicit TickToCandleResampler(Timeframe tf = Timeframe::M1);

        // Feed a tick; emits a finished Candle when the time bucket rolls
        std::optional<fin::core::Candle> update(const fin::core::Tick &t);

        // Close the current partial candle (if any).
        std::optional<fin::core::Candle> flush();

    private:
        Timeframe tf_;
        bool has_open_ = false;

        fin::core::Timestamp bucket_start_{};
        double open_ = 0, high_ = 0, low_ = 0, close_ = 0;
        double vol_ = 0;

        std::optional<fin::core::Timestamp> last_ts_;

        fin::core::Timestamp bucket_floor(fin::core::Timestamp ts) const;
        fin::core::Timestamp bucket_end(fin::core::Timestamp ts) const;
    };

} // namespace fin::io

#endif // FIN_IO_RESAMPLER_HPP
