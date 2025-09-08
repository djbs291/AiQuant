#include "fin/indicators/FeatureBus.hpp"

namespace fin::indicators
{
    FeatureBus::FeatureBus(std::size_t ema_fast, std::size_t rsi_p,
                           std::size_t macd_fast, std::size_t macd_slow, std::size_t macd_signal)
        : ema_(ema_fast), rsi_(rsi_p), macd_(macd_fast, macd_slow, macd_signal) {}

    void FeatureBus::reset()
    {
        // If your EMA/RSI/MACD expose reset(), call them; else do nothing
        ema_.reset();
        rsi_.reset();
        macd_.reset();
    }

    std::optional<FeatureRow> FeatureBus::update(const fin::core::Candle &c)
    {
        const double close = c.close().value();

        auto e = ema_.update(close);
        // RSI consumes Price via its interface
        rsi_.update(fin::core::Price(close));
        auto r = rsi_.is_ready() ? std::optional<double>(rsi_.value()) : std::nullopt;
        auto m = macd_.update(close);

        if (!e || !r || !m)
            return std::nullopt;

        return FeatureRow{
            c.start_time(),
            close,
            *e,
            *r,
            m->macd,
            m->signal,
            m->hist};
    }
}
