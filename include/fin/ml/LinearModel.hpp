#pragma once
#ifndef FIN_ML_LINEAR_MODEL_HPP
#define FIN_ML_LINEAR_MODEL_HPP

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fin/ml/IModel.hpp"

namespace fin::ml
{
    /**
     * @brief Minimal linear model for the MVP
     *
     * The model applies a weighted sum over the features followed by a bias term:
     * prediction = bias + dot(weights, features)
     *
     * Two setup options are exposed:
     *  1. Positional weights (the incoming FeatureVector must share the same
     *     ordering)
     *  2. Named weights, where each weight is bound to a specific feature
     *     name; missing features default to zero contribution
     *
     */
    class LinearModel final : public IModel
    {
    public:
        LinearModel() = default;
        LinearModel(std::vector<double> weights, double bias = 0.0);

        void reset() override;
        [[nodiscard]] bool is_ready() const override;
        [[nodiscard]] double predict(const FeatureVector &features) const override;

        void set_weights(std::vector<double> weights, double bias = 0.0);
        void set_named_weights(std::vector<std::pair<std::string, double>> weights, double bias = 0.0);

        // Parses a simple CSV-like configuration
        // bias, 0.12
        // feature_name, weight
        // ...
        bool load_from_file(const std::string &path);

    private:
        double bias_ = 0.0;
        std::vector<double> weights_{};
        std::vector<std::pair<std::string, double>> named_weights_{};
        bool ready_ = false;
    };
}

#endif // FIN_ML_LINEAR_MODEL_HPP
