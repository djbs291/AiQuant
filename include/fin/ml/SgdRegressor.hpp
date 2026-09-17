#pragma once
#ifndef FIN_ML_SGD_REGRESSOR_HPP
#define FIN_ML_SGD_REGRESSOR_HPP

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "fin/indicators/FeatureBus.hpp"
#include "fin/ml/IModel.hpp"
#include "fin/ml/LinearModel.hpp"

namespace fin::ml
{
    struct SgdOptions
    {
        // Step size at the first update. The schedule below decays it from here.
        double learning_rate = 0.01;
        // L2 penalty, applied to the weights but not to the bias.
        double l2 = 1e-6;
        // Passes over the batch in fit(). Ignored by partial_fit(), which is one sample.
        std::size_t epochs = 10;
        // Subtract the running mean and divide by the running standard deviation before
        // updating. Leave it on unless the features are already on a common scale: `close`
        // is ~100 while `macd` is ~0.01, and one learning rate cannot serve both.
        bool standardize = true;
        // eta_t = learning_rate / (1 + t)^power_t. Zero gives a constant rate.
        double power_t = 0.25;
    };

    /**
     * @brief Linear regressor fitted by stochastic gradient descent.
     *
     * This is the engine's first model that actually implements `IModel::fit` and
     * `IModel::partial_fit`: it can be trained on a batch and then keep learning one sample
     * at a time as new candles close, which the closed-form ridge path cannot do.
     *
     * Samples are visited in order and never shuffled. For a price series the order *is* the
     * signal, and keeping it also makes every run reproducible without an RNG seed.
     *
     * The standardizer keeps Welford moments over every sample it has seen, so its estimates
     * keep moving as the series does. That is what makes the online path work on raw
     * indicator values, and it means a prediction depends on the moments at the time it was
     * made: the same feature vector can score differently after further updates.
     */
    class SgdRegressor final : public IModel
    {
    public:
        SgdRegressor() = default;
        // Throws std::invalid_argument on a learning rate <= 0, a negative l2 or power_t.
        explicit SgdRegressor(SgdOptions options);

        void reset() override;
        [[nodiscard]] bool is_ready() const override;
        [[nodiscard]] double predict(const FeatureVector &features) const override;

        // Batch fit: `options().epochs` sequential passes over the samples. Unlike the ridge
        // solver this has no minimum sample count, because nothing is inverted here.
        void fit(std::span<const FeatureVector> X, std::span<const double> y) override;

        // One gradient step. The first call binds the feature schema; later calls with a
        // different feature set throw std::invalid_argument rather than learning nonsense.
        void partial_fit(const FeatureVector &features, double target) override;

        // The equivalent plain linear model, with the standardizer folded into the weights:
        // w'_j = w_j / s_j and bias' = bias - sum_j(w_j * m_j / s_j). Predictions match this
        // model exactly, so an SGD run can be written to, and reloaded from, the same model
        // file format the ridge path uses. Throws std::logic_error before the first update.
        [[nodiscard]] LinearModel to_linear_model() const;

        [[nodiscard]] const SgdOptions &options() const noexcept { return options_; }
        [[nodiscard]] const std::vector<std::string> &feature_names() const noexcept { return names_; }
        [[nodiscard]] const std::vector<double> &weights() const noexcept { return weights_; }
        [[nodiscard]] double bias() const noexcept { return bias_; }
        // Weight updates applied so far, which is also the t in the learning-rate schedule.
        [[nodiscard]] std::size_t updates() const noexcept { return updates_; }
        // The step size the next update will use.
        [[nodiscard]] double current_learning_rate() const noexcept;

    private:
        void bind_schema(const FeatureVector &features);
        void require_schema(const FeatureVector &features) const;
        void observe(const FeatureVector &features);
        [[nodiscard]] double center_at(std::size_t j) const;
        [[nodiscard]] double scale_at(std::size_t j) const;
        [[nodiscard]] std::vector<double> to_z(const FeatureVector &features) const;

        SgdOptions options_{};
        std::vector<std::string> names_{};
        std::vector<double> weights_{};
        double bias_ = 0.0;

        // Welford moments for the standardizer, one pair per column.
        std::vector<double> mean_{};
        std::vector<double> m2_{};
        std::size_t seen_ = 0;

        std::size_t updates_ = 0;
        bool ready_ = false;
    };

    struct SgdTrainingSummary
    {
        SgdRegressor model;
        double mse = 0.0;
        std::size_t samples = 0;
    };

    // Trains on the same target as the ridge path, next_close - close, so the two are
    // directly comparable. Throws std::runtime_error when there are fewer than two rows.
    SgdTrainingSummary train_sgd_from_feature_rows(
        const std::vector<fin::indicators::FeatureRow> &rows,
        SgdOptions options = {});

} // namespace fin::ml

#endif /* FIN_ML_SGD_REGRESSOR_HPP */
