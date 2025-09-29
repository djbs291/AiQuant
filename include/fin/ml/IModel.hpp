#pragma once
#ifndef FIN_ML_IMODEL_HPP
#define FIN_ML_IMODEL_HPP

#include <span>
#include <stdexcept>
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
    };
}

#endif /*FIN_ML_IMODEL_HPP*/
