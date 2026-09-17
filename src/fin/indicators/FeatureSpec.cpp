#include "fin/indicators/FeatureSpec.hpp"

#include <algorithm>

#include "fin/indicators/adapters/CandleAdapters.hpp"

namespace fin::indicators
{
    namespace
    {
        template <typename Adapter, typename... Args>
        std::unique_ptr<IIndicatorScalarCandle> make(Args &&...args)
        {
            return std::make_unique<Adapter>(std::forward<Args>(args)...);
        }
    }

    const std::vector<FeatureSpec> &feature_catalog()
    {
        static const std::vector<FeatureSpec> catalog = {
            {"close", [](const FeatureParams &) { return make<CloseFromCandle>(); }},
            {"sma", [](const FeatureParams &p) { return make<SMAFromCandle>(p.sma); }},
            {"ema_fast", [](const FeatureParams &p) { return make<EMAFromCandle>(p.ema_fast); }},
            {"ema_slow", [](const FeatureParams &p) { return make<EMAFromCandle>(p.ema_slow); }},
            {"rsi", [](const FeatureParams &p) { return make<RSIFromCandle>(p.rsi); }},
            {"macd", [](const FeatureParams &p) { return make<MACDLineFromCandle>(p.macd_fast, p.macd_slow, p.macd_signal); }},
            {"macd_signal", [](const FeatureParams &p) { return make<MACDSignalFromCandle>(p.macd_fast, p.macd_slow, p.macd_signal); }},
            {"macd_hist", [](const FeatureParams &p) { return make<MACDHistFromCandle>(p.macd_fast, p.macd_slow, p.macd_signal); }},
            {"bb_upper", [](const FeatureParams &p) { return make<BollingerUpperFromCandle>(p.bb_period, p.bb_k); }},
            {"bb_mid", [](const FeatureParams &p) { return make<BollingerMidFromCandle>(p.bb_period, p.bb_k); }},
            {"bb_lower", [](const FeatureParams &p) { return make<BollingerLowerFromCandle>(p.bb_period, p.bb_k); }},
            {"atr", [](const FeatureParams &p) { return make<ATRFromCandle>(p.atr); }},
            {"adx", [](const FeatureParams &p) { return make<ADXFromCandle>(p.adx); }},
            {"plus_di", [](const FeatureParams &p) { return make<ADXPlusDIFromCandle>(p.adx); }},
            {"minus_di", [](const FeatureParams &p) { return make<ADXMinusDIFromCandle>(p.adx); }},
            {"stoch_k", [](const FeatureParams &p) { return make<StochKFromCandle>(p.stoch_k, p.stoch_d); }},
            {"stoch_d", [](const FeatureParams &p) { return make<StochDFromCandle>(p.stoch_k, p.stoch_d); }},
            // VWAP is session-scoped: it accumulates until reset() and never warms up.
            {"vwap", [](const FeatureParams &) { return make<VWAPFromCandle>(); }},
            {"zscore", [](const FeatureParams &p) { return make<ZScoreFromCandle>(p.zscore); }},
            {"momentum", [](const FeatureParams &p) { return make<MomentumFromCandle>(p.momentum); }},
        };
        return catalog;
    }

    const FeatureSpec *find_feature(std::string_view name)
    {
        const auto &catalog = feature_catalog();
        const auto it = std::find_if(catalog.begin(), catalog.end(),
                                     [name](const FeatureSpec &spec) { return spec.name == name; });
        return it == catalog.end() ? nullptr : &*it;
    }

    const std::vector<std::string> &default_feature_names()
    {
        // The set FeatureBus emitted before the feature list existed, in the same order,
        // so a scenario without a `features` key trains exactly the same model as before.
        static const std::vector<std::string> defaults = {
            "close", "ema_fast", "rsi", "macd", "macd_signal", "macd_hist"};
        return defaults;
    }
}
