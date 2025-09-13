#include "fin/signal/SignalEngine.hpp"

namespace fin::signal
{
    Signal SignalEngine::eval(const IndicatorsSnapshot &snap, std::optional<double> prediction) const
    {
        double score = 0.0;
        std::string reason;

        // RSI contribution
        if (snap.rsi.has_value())
        {
            if (*snap.rsi <= cfg_.rsi_buy_below)
            {
                score += 1.0;
                reason += "RSI<=buy ";
            }
            else if (*snap.rsi >= cfg_.rsi_sell_above)
            {
                score -= 1.0;
                reason += "RSI>=sell ";
            }
        }

        // EMA crossover contribution
        if (cfg_.use_ema_crossover && snap.ema_fast.has_value() && snap.ema_slow.has_value())
        {
            if (*snap.ema_fast > *snap.ema_slow)
            {
                score += 1.0;
                reason += "EMA+ ";
            }
            else if (*snap.ema_fast < *snap.ema_slow)
            {
                score -= 1.0;
                reason += "EMA- ";
            }
        }

        // Prediction contribution (sign only)
        if (prediction.has_value())
        {
            if (*prediction > 0)
            {
                score += 0.5; // smaller weight for model by default
                reason += "ML- ";
            }
            else if (*prediction < 0)
            {
                score -= 0.5;
                reason += "ML- ";
            }
        }

        Signal out;
        out.ts = snap.ts;
        out.symbol = snap.symbol;
        out.score = score;
        out.source = reason.empty() ? std::string{"rules"} : reason;
        out.type = (score > 0.0) ? SignalType::Buy : (score < 0.0 ? SignalType::Sell : SignalType::Hold);
        return out;
    }
}
