#include "fin/indicators/FeatureBus.hpp"

#include <stdexcept>

namespace fin::indicators
{
    FeatureBus::FeatureBus(std::size_t ema_fast, std::size_t rsi_p,
                           std::size_t macd_fast, std::size_t macd_slow, std::size_t macd_signal)
        : FeatureBus(default_feature_names(), [&] {
              FeatureParams p{};
              p.ema_fast = ema_fast;
              p.rsi = rsi_p;
              p.macd_fast = macd_fast;
              p.macd_slow = macd_slow;
              p.macd_signal = macd_signal;
              return p;
          }())
    {
    }

    FeatureBus::FeatureBus(const std::vector<std::string> &names, const FeatureParams &params)
    {
        if (names.empty())
            throw std::invalid_argument("FeatureBus needs at least one feature");

        auto schema = std::make_shared<FeatureSchema>();
        schema->names = names;
        indicators_.reserve(names.size());

        for (const auto &name : names)
        {
            const FeatureSpec *spec = find_feature(name);
            if (spec == nullptr)
                throw std::invalid_argument("Unknown feature: " + name);
            indicators_.push_back(spec->make(params));
        }

        schema_ = std::move(schema);
    }

    void FeatureBus::reset()
    {
        for (auto &indicator : indicators_)
            indicator->reset();
    }

    std::optional<FeatureRow> FeatureBus::update(const fin::core::Candle &c)
    {
        for (auto &indicator : indicators_)
            indicator->update(c);

        // All-or-nothing warmup: one row only once every selected indicator has a value.
        for (const auto &indicator : indicators_)
        {
            if (!indicator->is_ready())
                return std::nullopt;
        }

        FeatureRow row;
        row.ts = c.start_time();
        row.close = c.close().value();
        row.schema = schema_;
        row.values.reserve(indicators_.size());
        for (const auto &indicator : indicators_)
            row.values.push_back(indicator->value());

        return row;
    }
}
