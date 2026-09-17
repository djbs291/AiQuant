#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fin/core/Candle.hpp"

#include "fin/indicators/FeatureSpec.hpp"
#include "fin/indicators/IIndicatorCandle.hpp"

namespace fin::indicators
{
    // The ordered feature names a bus emits. Shared by every row it produces, so a row can
    // be turned into a named FeatureVector without carrying its own copy of the names.
    struct FeatureSchema
    {
        std::vector<std::string> names;
    };

    struct FeatureRow
    {
        fin::core::Timestamp ts; // candle start
        // Kept out of `values` on purpose: the training target is close[i+1] - close[i], which
        // must stay well defined even when a scenario does not select "close" as a feature.
        double close = 0.0;
        std::vector<double> values;
        std::shared_ptr<const FeatureSchema> schema;
    };

    class FeatureBus
    {
    public:
        // Historical constructor: the default six features with these periods.
        FeatureBus(std::size_t ema_fast = 12, std::size_t rsi_p = 14,
                   std::size_t macd_fast = 12, std::size_t macd_slow = 26, std::size_t macd_signal = 9);

        // Throws std::invalid_argument when a name is not in feature_catalog(), and when the
        // list is empty — an empty feature set would train a bias-only model in silence.
        FeatureBus(const std::vector<std::string> &names, const FeatureParams &params);

        void reset();

        // Emits a row only when *all* selected indicators are ready.
        std::optional<FeatureRow> update(const fin::core::Candle &c);

        const FeatureSchema &schema() const noexcept { return *schema_; }

    private:
        std::shared_ptr<const FeatureSchema> schema_;
        std::vector<std::unique_ptr<IIndicatorScalarCandle>> indicators_;
    };
}
