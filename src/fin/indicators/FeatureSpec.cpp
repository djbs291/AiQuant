#include "fin/indicators/FeatureSpec.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "fin/indicators/adapters/CandleAdapters.hpp"

namespace fin::indicators
{
    namespace
    {
        // One row per FeatureParams field: its key, and a pointer to it -- a period, or the
        // one real-valued parameter. Declaration order, which is the order a model file lists
        // them in.
        struct ParamField
        {
            std::string_view key;
            std::size_t FeatureParams::*period;
            double FeatureParams::*real;
        };

        const std::vector<ParamField> &param_fields()
        {
            static const std::vector<ParamField> fields = {
                {"sma", &FeatureParams::sma, nullptr},
                {"ema_fast", &FeatureParams::ema_fast, nullptr},
                {"ema_slow", &FeatureParams::ema_slow, nullptr},
                {"rsi", &FeatureParams::rsi, nullptr},
                {"macd_fast", &FeatureParams::macd_fast, nullptr},
                {"macd_slow", &FeatureParams::macd_slow, nullptr},
                {"macd_signal", &FeatureParams::macd_signal, nullptr},
                {"bb_period", &FeatureParams::bb_period, nullptr},
                {"bb_k", nullptr, &FeatureParams::bb_k},
                {"atr", &FeatureParams::atr, nullptr},
                {"adx", &FeatureParams::adx, nullptr},
                {"stoch_k", &FeatureParams::stoch_k, nullptr},
                {"stoch_d", &FeatureParams::stoch_d, nullptr},
                {"zscore", &FeatureParams::zscore, nullptr},
                {"momentum", &FeatureParams::momentum, nullptr},
            };
            return fields;
        }

        const ParamField *find_param(std::string_view key)
        {
            for (const auto &field : param_fields())
            {
                if (field.key == key)
                    return &field;
            }
            return nullptr;
        }

        template <typename Adapter, typename... Args>
        std::unique_ptr<IIndicatorScalarCandle> make(Args &&...args)
        {
            return std::make_unique<Adapter>(std::forward<Args>(args)...);
        }
    }

    const std::vector<FeatureSpec> &feature_catalog()
    {
        static const std::vector<FeatureSpec> catalog = {
            {"close", [](const FeatureParams &) { return make<CloseFromCandle>(); }, {}},
            {"sma", [](const FeatureParams &p) { return make<SMAFromCandle>(p.sma); }, {"sma"}},
            {"ema_fast", [](const FeatureParams &p) { return make<EMAFromCandle>(p.ema_fast); }, {"ema_fast"}},
            {"ema_slow", [](const FeatureParams &p) { return make<EMAFromCandle>(p.ema_slow); }, {"ema_slow"}},
            {"rsi", [](const FeatureParams &p) { return make<RSIFromCandle>(p.rsi); }, {"rsi"}},
            {"macd", [](const FeatureParams &p) { return make<MACDLineFromCandle>(p.macd_fast, p.macd_slow, p.macd_signal); }, {"macd_fast", "macd_slow", "macd_signal"}},
            {"macd_signal", [](const FeatureParams &p) { return make<MACDSignalFromCandle>(p.macd_fast, p.macd_slow, p.macd_signal); }, {"macd_fast", "macd_slow", "macd_signal"}},
            {"macd_hist", [](const FeatureParams &p) { return make<MACDHistFromCandle>(p.macd_fast, p.macd_slow, p.macd_signal); }, {"macd_fast", "macd_slow", "macd_signal"}},
            {"bb_upper", [](const FeatureParams &p) { return make<BollingerUpperFromCandle>(p.bb_period, p.bb_k); }, {"bb_period", "bb_k"}},
            {"bb_mid", [](const FeatureParams &p) { return make<BollingerMidFromCandle>(p.bb_period, p.bb_k); }, {"bb_period", "bb_k"}},
            {"bb_lower", [](const FeatureParams &p) { return make<BollingerLowerFromCandle>(p.bb_period, p.bb_k); }, {"bb_period", "bb_k"}},
            {"atr", [](const FeatureParams &p) { return make<ATRFromCandle>(p.atr); }, {"atr"}},
            {"adx", [](const FeatureParams &p) { return make<ADXFromCandle>(p.adx); }, {"adx"}},
            {"plus_di", [](const FeatureParams &p) { return make<ADXPlusDIFromCandle>(p.adx); }, {"adx"}},
            {"minus_di", [](const FeatureParams &p) { return make<ADXMinusDIFromCandle>(p.adx); }, {"adx"}},
            {"stoch_k", [](const FeatureParams &p) { return make<StochKFromCandle>(p.stoch_k, p.stoch_d); }, {"stoch_k", "stoch_d"}},
            {"stoch_d", [](const FeatureParams &p) { return make<StochDFromCandle>(p.stoch_k, p.stoch_d); }, {"stoch_k", "stoch_d"}},
            // VWAP is session-scoped: it accumulates until reset() and never warms up.
            {"vwap", [](const FeatureParams &) { return make<VWAPFromCandle>(); }, {}},
            {"zscore", [](const FeatureParams &p) { return make<ZScoreFromCandle>(p.zscore); }, {"zscore"}},
            {"momentum", [](const FeatureParams &p) { return make<MomentumFromCandle>(p.momentum); }, {"momentum"}},
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

    const std::vector<std::string_view> &feature_param_keys()
    {
        static const std::vector<std::string_view> keys = []
        {
            std::vector<std::string_view> out;
            for (const auto &field : param_fields())
                out.push_back(field.key);
            return out;
        }();
        return keys;
    }

    std::optional<double> get_feature_param(const FeatureParams &params, std::string_view key)
    {
        const ParamField *field = find_param(key);
        if (field == nullptr)
            return std::nullopt;
        return field->period ? static_cast<double>(params.*(field->period)) : params.*(field->real);
    }

    bool set_feature_param(FeatureParams &params, std::string_view key, double value)
    {
        const ParamField *field = find_param(key);
        if (field == nullptr || !std::isfinite(value))
            return false;

        if (field->period)
        {
            // Whole, positive, and small enough that the cast to size_t is exact: a period is
            // a count of bars, and anything past a trillion of them is corruption, not intent.
            if (value < 1.0 || value > 1e12 || std::floor(value) != value)
                return false;
            params.*(field->period) = static_cast<std::size_t>(value);
            return true;
        }

        if (value <= 0.0)
            return false;
        params.*(field->real) = value;
        return true;
    }

    std::vector<std::pair<std::string, double>> feature_params_for(const std::vector<std::string> &features,
                                                                   const FeatureParams &params)
    {
        std::vector<std::pair<std::string, double>> out;
        for (const auto &field : param_fields())
        {
            const bool used = std::any_of(features.begin(), features.end(), [&field](const std::string &name)
                                          {
                                              const FeatureSpec *spec = find_feature(name);
                                              return spec && std::find(spec->params.begin(), spec->params.end(),
                                                                       field.key) != spec->params.end();
                                          });
            if (used)
                out.emplace_back(std::string(field.key), *get_feature_param(params, field.key));
        }
        return out;
    }
}
