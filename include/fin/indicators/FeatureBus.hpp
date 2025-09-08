#pragma once
#include <optional>
#include <vector>

#include "fin/core/Candle.hpp"

#include "fin/indicators/EMA.hpp"
#include "fin/indicators/RSI.hpp"
#include "fin/indicators/MACD.hpp"

// Your adapters (close price from Candle, etc.)
#include "fin/indicators/adapters/CandleAdapters.hpp"
#include "fin/indicators/adapters/ScalarPricesAdapters.hpp"

namespace fin::indicators
{
    struct FeatureRow
    {
        fin::core::Timestamp ts; // candle start
        double close;
        double ema_fast;
        double rsi;
        double macd;
        double macd_signal;
        double macd_hist;
    };

    class FeatureBus
    {
    public:
        // tweak periods as you like
        FeatureBus(std::size_t ema_fast = 12, std::size_t rsi_p = 14,
                   std::size_t macd_fast = 12, std::size_t macd_slow = 26, std::size_t macd_signal = 9);

        void reset();

        // Emits a row only when *all* indicators are ready
        std::optional<FeatureRow> update(const fin::core::Candle &c);

    private:
        EMA ema_;
        RSI rsi_;
        MACD macd_;
    };

}
