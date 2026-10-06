#include "fin/signal/SignalEngine.hpp"
#include <optional>

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

        // What the rules alone decide, kept to tell whether the model changed anything.
        const double rules_score = score;

        // Prediction contribution (sign only), worth model_weight against 1 per rule.
        if (prediction.has_value() && cfg_.model_weight > 0.0)
        {
            if (*prediction > 0)
            {
                score += cfg_.model_weight;
                reason += "ML+ ";
            }
            else if (*prediction < 0)
            {
                score -= cfg_.model_weight;
                reason += "ML- ";
            }
        }

        Signal out;
        out.ts = snap.ts;
        out.symbol = snap.symbol;
        out.score = score;
        out.source = reason.empty() ? std::string{"rules"} : reason;
        const auto type_of = [](double value)
        { return (value > 0.0) ? SignalType::Buy : (value < 0.0 ? SignalType::Sell : SignalType::Hold); };
        out.type = type_of(score);
        out.model_decisive = out.type != type_of(rules_score);
        return out;
    }
}
