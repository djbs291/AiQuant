#include "fin/ml/FeatureVector.hpp"
#include "fin/indicators/FeatureBus.hpp"
#include <algorithm>

namespace fin::ml
{
    std::optional<double> FeatureVector::value_of(std::string_view name) const
    {
        if (names.empty())
            return std::nullopt;
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            if (names[i] == name)
            {
                return values[i];
            }
        }
        return std::nullopt;
    }

    FeatureVector FeatureVector::from_feature_row(const fin::indicators::FeatureRow &row)
    {
        FeatureVector fv;
        fv.ts = row.ts;
        fv.names = {"close", "ema_fast", "rsi", "macd", "macd_signal", "macd_hist"};
        fv.values = {row.close, row.ema_fast, row.rsi, row.macd, row.macd_signal, row.macd_hist};
        return fv;
    }
}
