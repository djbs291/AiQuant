#pragma once
#ifndef FIN_ML_MLP_REGRESSOR_HPP
#define FIN_ML_MLP_REGRESSOR_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "fin/indicators/FeatureBus.hpp"
#include "fin/ml/IModel.hpp"

namespace fin::ml
{
    enum class MlpActivation
    {
        Tanh,
        Relu
    };

    struct MlpOptions
    {
        // One entry per hidden layer, its width. The default is a single hidden layer of
        // eight units. Must be non-empty with every width >= 1: an MLP with no hidden layer
        // is just linear regression, which the ridge and sgd models already cover.
        std::vector<std::size_t> hidden_layers{8};
        // Constant step size. Unlike SgdRegressor there is no decay schedule: a deeper net is
        // sensitive enough to the rate that a fixed one is easier to reason about and tune.
        double learning_rate = 0.01;
        // L2 penalty on the weight matrices, never on the biases.
        double l2 = 1e-6;
        // Sequential passes over the batch in fit(). An MLP needs many more than the linear
        // models: the default is deliberately large.
        std::size_t epochs = 200;
        // Subtract the running mean and divide by the running standard deviation of each input
        // before the first layer. As with SgdRegressor, leave it on unless the features are
        // already on a common scale: one learning rate cannot serve `close` (~100) and `macd`
        // (~0.01) at once, and a saturating tanh makes the mismatch worse.
        bool standardize = true;
        // Hidden-layer nonlinearity. The output layer is always linear (this is regression).
        MlpActivation activation = MlpActivation::Tanh;
        // Seeds the weight initialization. Fixed by default, and never drawn from a clock or
        // random_device, so every run over the same data is bit-for-bit reproducible.
        std::uint64_t seed = 42;
    };

    /**
     * @brief Feed-forward multi-layer perceptron fitted by stochastic gradient descent.
     *
     * The engine's first model that is not linear: hidden layers with a tanh or relu
     * nonlinearity let it fit relationships the ridge and sgd paths cannot. The output layer
     * is linear, and it is trained on the same target as those two, next_close - close, so the
     * three are directly comparable.
     *
     * Like SgdRegressor it can be batch-fitted and then keep learning one sample at a time, it
     * standardizes its inputs from Welford moments kept over every sample it has seen, and it
     * visits samples in order without shuffling -- for a price series the order is the signal,
     * and keeping it makes a run reproducible without touching the weight-init seed.
     *
     * There is deliberately no `to_linear_model()`: an MLP does not fold into linear weights,
     * which is why it needs its own persistence and serving path (not yet implemented).
     */
    class MlpRegressor final : public IModel
    {
    public:
        MlpRegressor() = default;
        // Throws std::invalid_argument on an empty or zero-width hidden layer, a learning rate
        // <= 0, a negative l2, or zero epochs.
        explicit MlpRegressor(MlpOptions options);

        void reset() override;
        [[nodiscard]] bool is_ready() const override;
        [[nodiscard]] double predict(const FeatureVector &features) const override;

        // Batch fit: `options().epochs` sequential passes over the samples.
        void fit(std::span<const FeatureVector> X, std::span<const double> y) override;

        // One gradient step. The first call binds the feature schema and initializes the
        // weights; later calls with a different feature set throw std::invalid_argument.
        void partial_fit(const FeatureVector &features, double target) override;

        [[nodiscard]] const MlpOptions &options() const noexcept { return options_; }
        [[nodiscard]] const std::vector<std::string> &feature_names() const noexcept { return names_; }
        // Weight updates applied so far.
        [[nodiscard]] std::size_t updates() const noexcept { return updates_; }
        // Layer widths, input and output included: [n_features, h1, ..., 1]. Empty before the
        // first update binds the schema.
        [[nodiscard]] const std::vector<std::size_t> &layer_sizes() const noexcept { return layer_sizes_; }

    private:
        void bind_schema(const FeatureVector &features);
        void require_schema(const FeatureVector &features) const;
        void observe(const FeatureVector &features);
        [[nodiscard]] double center_at(std::size_t j) const;
        [[nodiscard]] double scale_at(std::size_t j) const;
        [[nodiscard]] std::vector<double> to_z(const FeatureVector &features) const;
        [[nodiscard]] double activate(double x) const;
        [[nodiscard]] double activate_grad(double activated) const;
        // Forward pass over standardized input `z`, returning the activation of every layer,
        // acts[0] == z and acts.back() the linear output. Used by predict and partial_fit.
        [[nodiscard]] std::vector<std::vector<double>> forward(const std::vector<double> &z) const;

        MlpOptions options_{};
        std::vector<std::string> names_{};

        // layer_sizes_[0] is the input width, layer_sizes_.back() is 1. One weight matrix and
        // one bias vector per layer transition. weights_[l] is row-major, out * in.
        std::vector<std::size_t> layer_sizes_{};
        std::vector<std::vector<double>> weights_{};
        std::vector<std::vector<double>> biases_{};

        // Welford moments for the input standardizer, one pair per input column.
        std::vector<double> mean_{};
        std::vector<double> m2_{};
        std::size_t seen_ = 0;

        std::size_t updates_ = 0;
        bool ready_ = false;
    };

    struct MlpTrainingSummary
    {
        MlpRegressor model;
        double mse = 0.0;
        std::size_t samples = 0;
    };

    // Trains on the same target as the ridge and sgd paths, next_close - close, so the three
    // are directly comparable. Throws std::runtime_error when there are fewer than two rows.
    MlpTrainingSummary train_mlp_from_feature_rows(
        const std::vector<fin::indicators::FeatureRow> &rows,
        MlpOptions options = {});

} // namespace fin::ml

#endif /* FIN_ML_MLP_REGRESSOR_HPP */
