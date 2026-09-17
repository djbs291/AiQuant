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
        // The names travel with the row's schema, so any feature set works here unchanged.
        if (row.schema)
            fv.names = row.schema->names;
        fv.values = row.values;
        return fv;
    }
}
