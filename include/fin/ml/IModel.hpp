#pragma once
#ifndef FIN_ML_IMODEL_HPP
#define FIN_ML_IMODEL_HPP

#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "fin/ml/FeatureVector.hpp"

namespace fin::ml
{
    /**
     * @brief Base Interface for statistical / machine-learning models used by the
     * engine. The MVP only requires inference, but the interface allows
     * implementations to expose simple training routines when needed
     * 
     */

    class IModel
    {
    public:
        virtual ~IModel() = default;

        // Clears any internal state (learned parameters, caches, etc. ).
        virtual void reset() = 0;

        // Returns true once the model is ready to emit predictions.
        [[nodiscard]] virtual bool is_ready() const = 0;

        // Computes a prediction for the supplied feature vector
        [[nodiscard]] virtual double predict(const FeatureVector &features) const = 0;

        // Optional batch fitting API (default: unsupported)
        virtual void fit(std::span<const FeatureVector>, std::span<const double>)
        {
            throw std::logic_error("fit() not implemented for this model");
        }

        // Optional online update API (default: unsupported)
        virtual void partial_fit(const FeatureVector &, double)
        {
            throw std::logic_error("partial_fit() not implemented for this model");
        }

        // Metadata a persisted model carries, so the serving paths (the stream, the backtest,
        // /predict and the per-symbol model directory) can read it off any IModel without
        // knowing whether it is linear or an MLP. The defaults are empty: a model that records
        // none of this -- an SgdRegressor, which is folded to a LinearModel before serving, or
        // a file written before a given field existed -- reads as "unknown", which every caller
        // already treats as "use what I was given".
        [[nodiscard]] virtual const std::vector<std::string> &feature_names() const
        {
            static const std::vector<std::string> empty;
            return empty;
        }
        [[nodiscard]] virtual const std::string &symbol() const
        {
            static const std::string empty;
            return empty;
        }
        [[nodiscard]] virtual const std::vector<std::pair<std::string, double>> &training_params() const
        {
            static const std::vector<std::pair<std::string, double>> empty;
            return empty;
        }
        [[nodiscard]] virtual const std::string &timeframe() const
        {
            static const std::string empty;
            return empty;
        }

        // Throws std::invalid_argument when the vector's feature set differs from the one the
        // model was trained on. The default accepts anything; models that record a feature set
        // override it. predict() stays permissive on its own, so this is the hook a serving
        // path calls when a mismatched model should be refused rather than scored on a subset.
        virtual void validate_schema(const FeatureVector &) const {}
    };
}

#endif /*FIN_ML_IMODEL_HPP*/
