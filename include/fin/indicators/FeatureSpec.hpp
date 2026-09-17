#pragma once
#ifndef FIN_INDICATORS_FEATURE_SPEC_HPP
#define FIN_INDICATORS_FEATURE_SPEC_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
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
    };

    // Every feature name a scenario may list, in catalogue order.
    const std::vector<FeatureSpec> &feature_catalog();

    // nullptr when the name is not in the catalogue.
    const FeatureSpec *find_feature(std::string_view name);

    // The historical set, kept as the default so existing scenarios are unaffected.
    const std::vector<std::string> &default_feature_names();
}

#endif // FIN_INDICATORS_FEATURE_SPEC_HPP
