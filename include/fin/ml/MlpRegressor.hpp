#pragma once
#ifndef FIN_ML_MLP_REGRESSOR_HPP
#define FIN_ML_MLP_REGRESSOR_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
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

        // Throws std::invalid_argument when the vector's feature set differs from the one the
        // model was bound to, mirroring LinearModel so a serving path can refuse a mismatch.
        void validate_schema(const FeatureVector &features) const override;

        [[nodiscard]] const MlpOptions &options() const noexcept { return options_; }
        [[nodiscard]] const std::vector<std::string> &feature_names() const noexcept override { return names_; }
        // Weight updates applied so far.
        [[nodiscard]] std::size_t updates() const noexcept { return updates_; }
        // Layer widths, input and output included: [n_features, h1, ..., 1]. Empty before the
        // first update binds the schema.
        [[nodiscard]] const std::vector<std::size_t> &layer_sizes() const noexcept { return layer_sizes_; }

        // Persisted metadata, mirroring LinearModel so the serving paths read it off either
        // model type through IModel. Set by run_scenario after training; written to, and read
        // back from, the model file.
        [[nodiscard]] const std::string &symbol() const noexcept override { return symbol_; }
        void set_symbol(std::string symbol) { symbol_ = std::move(symbol); }
        [[nodiscard]] const std::vector<std::pair<std::string, double>> &training_params() const noexcept override { return training_params_; }
        void set_training_params(std::vector<std::pair<std::string, double>> params) { training_params_ = std::move(params); }
        [[nodiscard]] const std::string &timeframe() const noexcept override { return timeframe_; }
        void set_timeframe(std::string timeframe) { timeframe_ = std::move(timeframe); }

        // Accessors the persistence code reads. The standardizer is stored as its Welford
        // moments so a reloaded model standardizes, and therefore predicts, bit-for-bit as the
        // trained one did.
        [[nodiscard]] const std::vector<std::vector<double>> &weights() const noexcept { return weights_; }
        [[nodiscard]] const std::vector<std::vector<double>> &biases() const noexcept { return biases_; }
        [[nodiscard]] const std::vector<double> &standardizer_mean() const noexcept { return mean_; }
        [[nodiscard]] const std::vector<double> &standardizer_m2() const noexcept { return m2_; }
        [[nodiscard]] std::size_t samples_seen() const noexcept { return seen_; }

        // Rebuilds a model from persisted state, as a frozen predictor: no further training is
        // expected, so the moments are taken as given rather than continued. Throws
        // std::invalid_argument when the pieces do not fit together. Used by load_mlp_model.
        static MlpRegressor from_state(MlpOptions options,
                                       std::vector<std::string> names,
                                       std::vector<std::size_t> layer_sizes,
                                       std::vector<std::vector<double>> weights,
                                       std::vector<std::vector<double>> biases,
                                       std::vector<double> mean,
                                       std::vector<double> m2,
                                       std::size_t seen);

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

        // Persisted metadata (feature names live in names_).
        std::string symbol_{};
        std::vector<std::pair<std::string, double>> training_params_{};
        std::string timeframe_{};
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

    // Writes the model to `path` in AiQuant's MLP file format: a "# type: mlp" header, the same
    // metadata comments a linear model file carries (# features / # symbol / # timeframe /
    // # params), then the architecture, the standardizer moments and every weight. Returns
    // false if the file cannot be opened or the model has not been trained. The counterpart to
    // save_linear_model; an MLP cannot use that one because it does not fold into linear weights.
    bool save_mlp_model(const MlpRegressor &model, const std::string &path);

    // Loads a model written by save_mlp_model. Returns false, leaving `out` untouched, when the
    // file cannot be opened or is not a well-formed MLP file -- these files are machine-written,
    // so anything that does not parse means corruption, as in LinearModel::load_from_file.
    bool load_mlp_model(MlpRegressor &out, const std::string &path);

    // Peeks a model file's "# type:" line without fully parsing it: "mlp" for an MLP file,
    // "linear" for anything else (an explicit "# type: linear", or no type line at all, which
    // is every model file written before this existed). Used by the polymorphic loader to pick
    // the right parser. Returns false only when the file cannot be opened.
    bool peek_model_type(const std::string &path, std::string &type_out);

    // Loads a model file of either kind, dispatching on peek_model_type, and returns it as an
    // IModel the serving paths predict through and read metadata off. Null on failure, with a
    // message in `error`.
    std::shared_ptr<IModel> load_model_file(const std::string &path, std::string &error);

    // The polymorphic counterpart to load_linear_model_dir: loads every `*.csv` in `dir` as one
    // symbol's model (keyed on the file name without its extension) through load_model_file, so a
    // directory may mix linear and MLP models. Refuses, naming the file, one that does not load
    // and one whose recorded symbol differs from its name; refuses a missing or empty directory.
    // Dotfiles and other extensions are ignored. On failure `out` is left untouched.
    bool load_model_dir(const std::string &dir,
                        std::unordered_map<std::string, std::shared_ptr<IModel>> &out,
                        std::string &error);

} // namespace fin::ml

#endif /* FIN_ML_MLP_REGRESSOR_HPP */
