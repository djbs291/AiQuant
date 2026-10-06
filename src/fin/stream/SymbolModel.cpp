#include "fin/stream/SymbolModel.hpp"

#include <sstream>
#include <memory>
#include <utility>
#include <vector>

namespace fin::stream
{
    namespace
    {
        std::string format_value(double value)
        {
            std::ostringstream out;
            out << value;
            return out.str();
        }

        std::string join(const std::vector<std::string> &names)
        {
            std::string out;
            for (std::size_t i = 0; i < names.size(); ++i)
                out += (i == 0 ? "" : ",") + names[i];
            return out;
        }
    } // namespace

    bool symbol_model_from(std::shared_ptr<fin::ml::IModel> model, const ModelOverrides &overrides,
                           const fin::indicators::FeatureParams &base, SymbolModel &out, std::string &error)
    {
        SymbolModel resolved;

        // Features: the recorded set, unless the file has none, in which case the override
        // (or, left empty, the stream's own) applies.
        const auto &recorded_features = model->feature_names();
        if (!recorded_features.empty() && !overrides.features.empty() && overrides.features != recorded_features)
        {
            error = "features " + join(overrides.features) + " were asked for, but the model was trained on " +
                    join(recorded_features);
            return false;
        }
        resolved.features = recorded_features.empty() ? overrides.features : recorded_features;

        // Parameters: the base with every explicit override applied, then every recorded value
        // on top -- after checking that no override disagrees with it.
        fin::indicators::FeatureParams params = base;
        for (const auto &[key, value] : overrides.params)
        {
            if (!fin::indicators::set_feature_param(params, key, value))
            {
                error = "invalid value for " + key + ": " + format_value(value);
                return false;
            }
        }
        for (const auto &[key, value] : model->training_params())
        {
            for (const auto &[override_key, override_value] : overrides.params)
            {
                if (override_key == key && override_value != value)
                {
                    error = key + " " + format_value(override_value) + " was asked for, but the model was trained with " +
                            key + " = " + format_value(value);
                    return false;
                }
            }
            // Already validated when the file was loaded.
            fin::indicators::set_feature_param(params, key, value);
        }
        resolved.params = params;

        // Timeframe: the same rule.
        if (!model->timeframe().empty())
        {
            const auto recorded = fin::io::timeframe_from_token(model->timeframe());
            if (!recorded)
            {
                error = "the model records an unknown timeframe '" + model->timeframe() + "'";
                return false;
            }
            if (overrides.timeframe && *overrides.timeframe != *recorded)
            {
                error = std::string("timeframe ") + fin::io::timeframe_token(*overrides.timeframe) +
                        " was asked for, but the model was trained on " + model->timeframe() + " candles";
                return false;
            }
            resolved.timeframe = recorded;
        }
        else
        {
            resolved.timeframe = overrides.timeframe;
        }

        resolved.model = std::move(model);
        out = std::move(resolved);
        return true;
    }

} // namespace fin::stream
