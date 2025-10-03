#pragma once
#ifndef FIN_ML_LINEAR_TRAINER_HPP
#define FIN_ML_LINEAR_TRAINER_HPP

#include <string>
#include <vector>

#include "fin/indicators/FeatureBus.hpp"
#include "fin/ml/LinearModel.hpp"

namespace fin::ml
{
    struct LinearTrainingOptions
    {
        // Set to true to normalize features by subtracting the sample mean
        bool center_features = false;
        double ridge_lambda = 1e-6;
    };

    struct LinearTrainingSummary
    {
        LinearModel model;
        double mse = 0.0;
        std::size_t samples = 0;
    };

    // Trains a linear model that predicts the next close-price delta
    // using FeatureBus-produced rows. Throws std::runtime-error on failure.
    LinearTrainingSummary
    train_linear_from_feature_rows(
        const std::vector<fin::indicators::FeatureRow> &rows,
        LinearTrainingOptions options = {});

    // Saves a linear model to disk in the CSV format consumed by LinearModel::load_from_file();
    bool save_linear_model(const LinearModel &model, const std::string &path);

} // namespace fin::ml

#endif /* FIN_ML_LINEAR_TRAINER_HPP */
