#pragma once
#ifndef FIN_INDICATORS_FEATURE_SPEC_HPP
#define FIN_INDICATORS_FEATURE_SPEC_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fin/indicators/IIndicatorCandle.hpp"

namespace fin::indicators
{
    // Periods for every indicator a feature can be built from. Defaults match the
    // indicators' own defaults, so a scenario only sets what it cares about.
    struct FeatureParams
    {
        std::size_t sma = 14;
        std::size_t ema_fast = 12;
        std::size_t ema_slow = 26;
        std::size_t rsi = 14;
        std::size_t macd_fast = 12;
        std::size_t macd_slow = 26;
        std::size_t macd_signal = 9;
        std::size_t bb_period = 20;
        double bb_k = 2.0;
        std::size_t atr = 14;
        std::size_t adx = 14;
        std::size_t stoch_k = 14;
        std::size_t stoch_d = 3;
        std::size_t zscore = 20;
        std::size_t momentum = 10;
    };

    using IndicatorFactory = std::unique_ptr<IIndicatorScalarCandle> (*)(const FeatureParams &);

    struct FeatureSpec
    {
        std::string_view name;
        IndicatorFactory make;
        // The FeatureParams fields `make` reads, by key (see feature_param_keys). A model file
        // records exactly these, so it can be rebuilt with the periods it was trained on.
        std::vector<std::string_view> params;
    };

    // Every feature name a scenario may list, in catalogue order.
    const std::vector<FeatureSpec> &feature_catalog();

    // nullptr when the name is not in the catalogue.
    const FeatureSpec *find_feature(std::string_view name);

    // The historical set, kept as the default so existing scenarios are unaffected.
    const std::vector<std::string> &default_feature_names();

    // Every FeatureParams field, by the name it goes by in a model file and a scenario, in
    // declaration order. The one table both the writer and the reader of "# params:" use.
    const std::vector<std::string_view> &feature_param_keys();

    // nullopt for an unknown key.
    std::optional<double> get_feature_param(const FeatureParams &params, std::string_view key);

    // False, leaving `params` unchanged, for an unknown key or a value that cannot be one: a
    // period must be a whole number >= 1, and bb_k finite and > 0.
    bool set_feature_param(FeatureParams &params, std::string_view key, double value);

    // The parameters the named features read, each once, in feature_param_keys() order, with
    // their values in `params`. Unknown names contribute nothing.
    std::vector<std::pair<std::string, double>> feature_params_for(const std::vector<std::string> &features,
                                                                   const FeatureParams &params);
}

#endif // FIN_INDICATORS_FEATURE_SPEC_HPP
