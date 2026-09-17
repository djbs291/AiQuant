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

        // Throws std::invalid_argument when the vector's feature set differs from the one the
        // model file recorded. predict() stays permissive (it skips names it does not know), so
        // call this on paths where a mismatched model should be refused rather than silently
        // producing a prediction from a subset of the weights.
        void validate_schema(const FeatureVector &features) const;

        [[nodiscard]] double bias() const noexcept { return bias_; }
        [[nodiscard]] const std::vector<double> &weights() const noexcept { return weights_; }
        [[nodiscard]] const std::vector<std::pair<std::string, double>> &named_weights() const noexcept { return named_weights_; }
        // Feature order recorded by the model file, empty for files written before it existed.
        [[nodiscard]] const std::vector<std::string> &feature_names() const noexcept { return feature_names_; }

    private:
        double bias_ = 0.0;
        std::vector<double> weights_{};
        std::vector<std::pair<std::string, double>> named_weights_{};
        std::vector<std::string> feature_names_{};
        bool ready_ = false;
    };
}

#endif // FIN_ML_LINEAR_MODEL_HPP
