#include "fin/api/PredictService.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include "fin/ml/FeatureVector.hpp"
#include "fin/ml/LinearModel.hpp"

namespace fin::api
{
    namespace
    {
        std::string join(const std::vector<std::string> &names)
        {
            std::string out;
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                if (i > 0)
                    out += ", ";
                out += names[i];
            }
            return out;
        }

        const double *find_feature(const std::vector<std::pair<std::string, double>> &features,
                                   const std::string &name)
        {
            const auto it = std::find_if(features.begin(), features.end(),
                                         [&name](const auto &entry) { return entry.first == name; });
            return it == features.end() ? nullptr : &it->second;
        }

        // Orders the caller's values the way the model expects. When the model records no
        // feature set (a file written before that existed) the caller's own order is used.
        fin::ml::FeatureVector build_vector(const fin::ml::LinearModel &model,
                                            const PredictRequest &request,
                                            std::vector<std::string> &order)
        {
            fin::ml::FeatureVector fv;
            const auto &expected = model.feature_names();

            if (expected.empty())
            {
                for (const auto &[name, value] : request.features)
                {
                    fv.names.push_back(name);
                    fv.values.push_back(value);
                }
                order = fv.names;
                return fv;
            }

            std::vector<std::string> missing;
            for (const auto &name : expected)
            {
                if (const double *value = find_feature(request.features, name))
                {
                    fv.names.push_back(name);
                    fv.values.push_back(*value);
                }
                else
                {
                    missing.push_back(name);
                }
            }

            if (!missing.empty())
            {
                throw std::invalid_argument("Missing feature values for [" + join(missing) +
                                            "]; the model was trained on [" + join(expected) + "]");
            }

            // Extras are refused rather than ignored: silently dropping a name the caller
            // believes is being used would be the same footgun predict() already has.
            std::vector<std::string> unexpected;
            for (const auto &[name, value] : request.features)
            {
                (void)value;
                if (std::find(expected.begin(), expected.end(), name) == expected.end())
                    unexpected.push_back(name);
            }
            if (!unexpected.empty())
            {
                throw std::invalid_argument("Unexpected feature values for [" + join(unexpected) +
                                            "]; the model was trained on [" + join(expected) + "]");
            }

            order = fv.names;
            return fv;
        }
    }

    PredictService::PredictService(std::string default_model_path)
        : default_model_path_(std::move(default_model_path))
    {
    }

    PredictResponse PredictService::predict(const PredictRequest &request) const
    {
        const std::string path = request.model_path.empty() ? default_model_path_ : request.model_path;
        if (path.empty())
        {
            throw std::invalid_argument(
                "No model available: pass \"model\" in the request or start the service with --model");
        }

        if (request.features.empty())
            throw std::invalid_argument("No feature values supplied");

        fin::ml::LinearModel model;
        if (!model.load_from_file(path))
            throw std::invalid_argument("Failed to load model: " + path);

        std::vector<std::string> order;
        const auto fv = build_vector(model, request, order);
        model.validate_schema(fv);

        PredictResponse response;
        response.prediction = model.predict(fv);
        response.features = std::move(order);
        response.model_path = path;
        return response;
    }

    SignalResponse PredictService::signal(const SignalRequest &request) const
    {
        SignalResponse response;

        // An explicit prediction wins, so the rules can be evaluated with no model at all.
        if (request.prediction)
        {
            response.prediction = request.prediction;
        }
        else if (!request.predict.features.empty())
        {
            const auto predicted = predict(request.predict);
            response.prediction = predicted.prediction;
            response.features = predicted.features;
        }

        fin::signal::IndicatorsSnapshot snapshot;
        if (request.ts_ms)
        {
            snapshot.ts = fin::core::Timestamp{std::chrono::milliseconds{*request.ts_ms}};
        }
        snapshot.symbol = request.symbol;
        snapshot.close = request.close;
        snapshot.rsi = request.rsi;
        snapshot.ema_fast = request.ema_fast;
        snapshot.ema_slow = request.ema_slow;

        const fin::signal::SignalEngine engine{request.engine};
        response.signal = engine.eval(snapshot, response.prediction);
        return response;
    }
}
