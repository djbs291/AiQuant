#pragma once
#ifndef FIN_ML_LINEAR_MODEL_HPP
#define FIN_ML_LINEAR_MODEL_HPP

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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
        void validate_schema(const FeatureVector &features) const override;

        [[nodiscard]] double bias() const noexcept { return bias_; }
        [[nodiscard]] const std::vector<double> &weights() const noexcept { return weights_; }
        [[nodiscard]] const std::vector<std::pair<std::string, double>> &named_weights() const noexcept { return named_weights_; }
        // Feature order recorded by the model file, empty for files written before it existed.
        [[nodiscard]] const std::vector<std::string> &feature_names() const noexcept override { return feature_names_; }

        // The instrument the model was trained on, from a "# symbol:" line; empty for files
        // written before it existed, or trained on a file whose symbol nobody resolved.
        [[nodiscard]] const std::string &symbol() const noexcept override { return symbol_; }
        void set_symbol(std::string symbol) { symbol_ = std::move(symbol); }

        // The indicator parameters the model's features were computed with, from a
        // "# params: rsi=10,atr=7" line: exactly the ones its features read, keyed as in
        // fin::indicators::feature_param_keys(). Empty for files written before it existed.
        [[nodiscard]] const std::vector<std::pair<std::string, double>> &training_params() const noexcept override { return training_params_; }
        void set_training_params(std::vector<std::pair<std::string, double>> params) { training_params_ = std::move(params); }

        // The candle timeframe the model was trained on ("M1", ...), from "# timeframe:".
        // Kept as the token: fin_ml does not depend on fin_io, which owns the enum.
        [[nodiscard]] const std::string &timeframe() const noexcept override { return timeframe_; }
        void set_timeframe(std::string timeframe) { timeframe_ = std::move(timeframe); }

    private:
        double bias_ = 0.0;
        std::vector<double> weights_{};
        std::vector<std::pair<std::string, double>> named_weights_{};
        std::vector<std::string> feature_names_{};
        std::string symbol_{};
        std::vector<std::pair<std::string, double>> training_params_{};
        std::string timeframe_{};
        bool ready_ = false;
    };

    // Loads every `*.csv` in `dir` as one symbol's model, keyed on the file name without its
    // extension (`models/ABC.csv` -> "ABC"). Everything is loaded up front, so a bad file fails
    // at startup rather than mid-feed, and no path is ever built from a symbol a feed supplied.
    // Refuses, naming the file: one that does not load, and one whose recorded symbol differs
    // from its name -- a model trained on ABC saved as XYZ.csv is the very mistake per-symbol
    // models exist to prevent. Also refuses a missing directory or one with no model in it.
    // Dotfiles and other extensions are ignored. On failure `out` is left untouched.
    bool load_linear_model_dir(const std::string &dir,
                               std::unordered_map<std::string, std::shared_ptr<LinearModel>> &out,
                               std::string &error);
}

#endif // FIN_ML_LINEAR_MODEL_HPP
