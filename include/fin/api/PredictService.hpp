#pragma once
#ifndef FIN_API_PREDICT_SERVICE_HPP
#define FIN_API_PREDICT_SERVICE_HPP

// Single-shot prediction and signal evaluation, without running a whole scenario.
//
// This is the layer behind the /predict and /signal endpoints. It is deliberately free of
// transport and formatting concerns so it can be exercised from ctest, and later from the
// Python module, without standing up a server.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fin/signal/Signal.hpp"
#include "fin/signal/SignalEngine.hpp"

namespace fin::api
{
    struct PredictRequest
    {
        // Model file to use. Empty means "the one the service was constructed with".
        // The caller is responsible for confining this path before it gets here.
        std::string model_path;
        // Feature values by name, in the order the caller supplied them.
        std::vector<std::pair<std::string, double>> features;
    };

    struct PredictResponse
    {
        double prediction = 0.0;
        // The feature order actually fed to the model, which is the model's own when it
        // records one.
        std::vector<std::string> features;
        std::string model_path;
    };

    struct SignalRequest
    {
        // Optional model input. When features are supplied and no explicit prediction is,
        // the model's output is fed to the signal engine.
        PredictRequest predict;

        std::string symbol;
        std::optional<long long> ts_ms;
        double close = 0.0;
        std::optional<double> rsi;
        std::optional<double> ema_fast;
        std::optional<double> ema_slow;

        // Overrides the model. Lets a caller evaluate the rules against a prediction it
        // already has, with no model file at all.
        std::optional<double> prediction;

        fin::signal::SignalEngineConfig engine;
    };

    struct SignalResponse
    {
        fin::signal::Signal signal;
        // The prediction that fed the engine, if any.
        std::optional<double> prediction;
        std::vector<std::string> features;
    };

    class PredictService
    {
    public:
        // `default_model_path` is the server's --model flag; empty when none was given.
        explicit PredictService(std::string default_model_path = {});

        // Throws std::invalid_argument when no model is available, the file cannot be read,
        // or the features do not match what the model was trained on.
        [[nodiscard]] PredictResponse predict(const PredictRequest &request) const;

        // Evaluates the rule engine, optionally with a model prediction.
        [[nodiscard]] SignalResponse signal(const SignalRequest &request) const;

        [[nodiscard]] const std::string &default_model_path() const noexcept { return default_model_path_; }

    private:
        std::string default_model_path_;
    };
}

#endif // FIN_API_PREDICT_SERVICE_HPP
