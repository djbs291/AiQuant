#include "fin/ml/MlpRegressor.hpp"

#include <cmath>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

#include "fin/ml/FeatureVector.hpp"

namespace fin::ml
{
    namespace
    {
        std::string join_names(const std::vector<std::string> &names)
        {
            std::string out;
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                if (i > 0)
                    out += ", ";
                out += names[i];
            }
            return out;
        }
    } // namespace

    MlpRegressor::MlpRegressor(MlpOptions options) : options_(std::move(options))
    {
        // Checked here rather than at the first update: these come from a scenario file, and a
        // degenerate architecture would train nothing while looking like it did.
        if (options_.hidden_layers.empty())
            throw std::invalid_argument("MlpRegressor: needs at least one hidden layer");
        for (std::size_t width : options_.hidden_layers)
        {
            if (width == 0)
                throw std::invalid_argument("MlpRegressor: every hidden layer must have width >= 1");
        }
        if (!(options_.learning_rate > 0.0))
            throw std::invalid_argument("MlpRegressor: learning_rate must be > 0");
        if (!(options_.l2 >= 0.0))
            throw std::invalid_argument("MlpRegressor: l2 must be >= 0");
        if (options_.epochs == 0)
            throw std::invalid_argument("MlpRegressor: epochs must be >= 1");
    }

    void MlpRegressor::reset()
    {
        names_.clear();
        layer_sizes_.clear();
        weights_.clear();
        biases_.clear();
        mean_.clear();
        m2_.clear();
        seen_ = 0;
        updates_ = 0;
        ready_ = false;
        // options_ survives: it is configuration, not learned state.
    }

    bool MlpRegressor::is_ready() const
    {
        return ready_;
    }

    void MlpRegressor::bind_schema(const FeatureVector &features)
    {
        if (features.values.empty())
            throw std::invalid_argument("MlpRegressor: cannot train on an empty feature vector");

        names_ = features.names;

        const std::size_t n_in = features.values.size();
        layer_sizes_.clear();
        layer_sizes_.push_back(n_in);
        for (std::size_t width : options_.hidden_layers)
            layer_sizes_.push_back(width);
        layer_sizes_.push_back(1); // linear scalar output

        // Deterministic init: a fixed-seed Mersenne Twister and Glorot/Xavier scaling, which
        // keeps the tanh units out of saturation at the start. Never a clock or random_device,
        // so a run over the same data is reproducible.
        std::mt19937_64 rng(options_.seed);
        weights_.clear();
        biases_.clear();
        for (std::size_t l = 0; l + 1 < layer_sizes_.size(); ++l)
        {
            const std::size_t fan_in = layer_sizes_[l];
            const std::size_t fan_out = layer_sizes_[l + 1];
            const double limit = std::sqrt(6.0 / static_cast<double>(fan_in + fan_out));
            std::uniform_real_distribution<double> dist(-limit, limit);

            std::vector<double> w(fan_out * fan_in, 0.0);
            for (double &value : w)
                value = dist(rng);
            weights_.push_back(std::move(w));
            biases_.emplace_back(fan_out, 0.0);
        }

        mean_.assign(n_in, 0.0);
        m2_.assign(n_in, 0.0);
        seen_ = 0;
    }

    void MlpRegressor::require_schema(const FeatureVector &features) const
    {
        if (layer_sizes_.empty() || features.values.size() != layer_sizes_.front())
        {
            const std::size_t expected = layer_sizes_.empty() ? 0 : layer_sizes_.front();
            throw std::invalid_argument("MlpRegressor: expected " + std::to_string(expected) +
                                        " features but got " + std::to_string(features.values.size()));
        }

        // A nameless vector is accepted positionally, as the other models do.
        if (!names_.empty() && !features.names.empty() && features.names != names_)
        {
            throw std::invalid_argument("MlpRegressor: feature set mismatch: bound to [" +
                                        join_names(names_) + "] but got [" +
                                        join_names(features.names) + "]");
        }
    }

    void MlpRegressor::observe(const FeatureVector &features)
    {
        if (!options_.standardize)
            return; // nothing to track: to_z() passes the raw values through

        ++seen_;
        const double n = static_cast<double>(seen_);
        for (std::size_t j = 0; j < mean_.size(); ++j)
        {
            const double x = features.values[j];
            const double delta = x - mean_[j];
            mean_[j] += delta / n;
            m2_[j] += delta * (x - mean_[j]);
        }
    }

    double MlpRegressor::center_at(std::size_t j) const
    {
        return options_.standardize ? mean_[j] : 0.0;
    }

    double MlpRegressor::scale_at(std::size_t j) const
    {
        if (!options_.standardize || seen_ < 2)
            return 1.0;

        const double variance = m2_[j] / static_cast<double>(seen_ - 1);
        const double sigma = std::sqrt(variance);
        // A constant column has sigma 0. Dividing by 1 leaves it centered at exactly zero, so
        // it contributes nothing instead of producing an infinity.
        return (sigma > 1e-12) ? sigma : 1.0;
    }

    std::vector<double> MlpRegressor::to_z(const FeatureVector &features) const
    {
        std::vector<double> z(features.values.size(), 0.0);
        for (std::size_t j = 0; j < z.size(); ++j)
            z[j] = (features.values[j] - center_at(j)) / scale_at(j);
        return z;
    }

    double MlpRegressor::activate(double x) const
    {
        switch (options_.activation)
        {
        case MlpActivation::Relu:
            return x > 0.0 ? x : 0.0;
        case MlpActivation::Tanh:
        default:
            return std::tanh(x);
        }
    }

    double MlpRegressor::activate_grad(double activated) const
    {
        switch (options_.activation)
        {
        case MlpActivation::Relu:
            // Derivative from the activation output: relu is positive iff its input was.
            return activated > 0.0 ? 1.0 : 0.0;
        case MlpActivation::Tanh:
        default:
            // d/dx tanh(x) = 1 - tanh(x)^2, expressed in the stored activation.
            return 1.0 - activated * activated;
        }
    }

    std::vector<std::vector<double>> MlpRegressor::forward(const std::vector<double> &z) const
    {
        const std::size_t layers = weights_.size();
        std::vector<std::vector<double>> acts;
        acts.reserve(layers + 1);
        acts.push_back(z);

        for (std::size_t l = 0; l < layers; ++l)
        {
            const std::size_t fan_in = layer_sizes_[l];
            const std::size_t fan_out = layer_sizes_[l + 1];
            const std::vector<double> &prev = acts.back();
            const std::vector<double> &w = weights_[l];
            const std::vector<double> &b = biases_[l];

            std::vector<double> next(fan_out, 0.0);
            for (std::size_t o = 0; o < fan_out; ++o)
            {
                double acc = b[o];
                const std::size_t base = o * fan_in;
                for (std::size_t i = 0; i < fan_in; ++i)
                    acc += w[base + i] * prev[i];
                // Every layer but the last is nonlinear; the output layer is linear.
                next[o] = (l + 1 < layers) ? activate(acc) : acc;
            }
            acts.push_back(std::move(next));
        }
        return acts;
    }

    double MlpRegressor::predict(const FeatureVector &features) const
    {
        if (!ready_)
            throw std::logic_error("MlpRegressor::predict() called before the first update");

        require_schema(features);
        const std::vector<std::vector<double>> acts = forward(to_z(features));
        return acts.back().front();
    }

    void MlpRegressor::fit(std::span<const FeatureVector> X, std::span<const double> y)
    {
        if (X.size() != y.size())
        {
            throw std::invalid_argument("MlpRegressor::fit() got " + std::to_string(X.size()) +
                                        " feature vectors and " + std::to_string(y.size()) + " targets");
        }
        if (X.empty())
            throw std::invalid_argument("MlpRegressor::fit() needs at least one sample");

        // Sequential passes, no shuffling: see the class comment.
        for (std::size_t epoch = 0; epoch < options_.epochs; ++epoch)
        {
            for (std::size_t i = 0; i < X.size(); ++i)
                partial_fit(X[i], y[i]);
        }
    }

    void MlpRegressor::partial_fit(const FeatureVector &features, double target)
    {
        if (layer_sizes_.empty())
            bind_schema(features);
        else
            require_schema(features);

        // Caught here, before the moments are touched: a single NaN folded into the running
        // mean poisons every later prediction, and the cause would be long gone by then.
        for (std::size_t j = 0; j < features.values.size(); ++j)
        {
            if (!std::isfinite(features.values[j]))
            {
                const std::string column = names_.empty() ? ("column " + std::to_string(j))
                                                          : ("feature '" + names_[j] + "'");
                throw std::invalid_argument("MlpRegressor::partial_fit(): " + column + " is not finite");
            }
        }
        if (!std::isfinite(target))
            throw std::invalid_argument("MlpRegressor::partial_fit(): target is not finite");

        // The sample's own moments go in before it is standardized. The features are known at
        // prediction time and only the target is not, so this adds no lookahead.
        observe(features);
        const std::vector<std::vector<double>> acts = forward(to_z(features));

        const double output = acts.back().front();
        const double error = output - target;
        if (!std::isfinite(error))
        {
            throw std::runtime_error("MlpRegressor diverged after " + std::to_string(updates_) +
                                     " updates: lower learning_rate (currently " +
                                     std::to_string(options_.learning_rate) + ")");
        }

        const std::size_t layers = weights_.size();
        const double eta = options_.learning_rate;

        // Backpropagation. delta[l] is dLoss/d(pre-activation) of layer l, with the loss
        // 0.5*(output - target)^2, so the output delta is the error itself.
        std::vector<double> delta = {error};
        for (std::size_t back = 0; back < layers; ++back)
        {
            const std::size_t l = layers - 1 - back;
            const std::size_t fan_in = layer_sizes_[l];
            const std::size_t fan_out = layer_sizes_[l + 1];
            const std::vector<double> &prev = acts[l];
            std::vector<double> &w = weights_[l];
            std::vector<double> &b = biases_[l];

            // The gradient into the previous layer's activation, accumulated before the weights
            // are updated in place. Empty for the input layer, which has no delta of its own.
            std::vector<double> prev_delta(l == 0 ? 0 : fan_in, 0.0);
            for (std::size_t o = 0; o < fan_out; ++o)
            {
                const double d = delta[o];
                const std::size_t base = o * fan_in;
                for (std::size_t i = 0; i < fan_in; ++i)
                {
                    if (l != 0)
                        prev_delta[i] += w[base + i] * d;
                    // Gradient descent with L2 on the weights only.
                    w[base + i] -= eta * (d * prev[i] + options_.l2 * w[base + i]);
                }
                b[o] -= eta * d;
            }

            if (l != 0)
            {
                // Fold in the derivative of the previous (hidden) layer's activation.
                for (std::size_t i = 0; i < fan_in; ++i)
                    prev_delta[i] *= activate_grad(prev[i]);
                delta = std::move(prev_delta);
            }
        }

        ++updates_;
        ready_ = true;
    }

    MlpTrainingSummary train_mlp_from_feature_rows(
        const std::vector<fin::indicators::FeatureRow> &rows,
        MlpOptions options)
    {
        if (rows.size() < 2)
            throw std::runtime_error("Need at least two feature rows to train an MLP model");

        const std::size_t samples = rows.size() - 1;

        std::vector<FeatureVector> features;
        features.reserve(samples);
        std::vector<double> targets;
        targets.reserve(samples);
        for (std::size_t i = 0; i < samples; ++i)
        {
            features.push_back(FeatureVector::from_feature_row(rows[i]));
            targets.push_back(rows[i + 1].close - rows[i].close);
        }

        MlpTrainingSummary summary{};
        summary.model = MlpRegressor(options);
        summary.model.fit(features, targets);
        summary.samples = samples;

        // Scored on the final weights, as the ridge and sgd paths do, so the three MSEs mean
        // the same thing.
        double mse = 0.0;
        for (std::size_t i = 0; i < samples; ++i)
        {
            const double err = summary.model.predict(features[i]) - targets[i];
            mse += err * err;
        }
        summary.mse = mse / static_cast<double>(samples);
        return summary;
    }

} // namespace fin::ml
