#include "fin/ml/SgdRegressor.hpp"

#include <cmath>
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

    SgdRegressor::SgdRegressor(SgdOptions options) : options_(options)
    {
        // Checked here rather than at the first update: these come from a scenario file, and
        // a zero learning rate or zero epochs would train nothing while looking like it did.
        if (!(options_.learning_rate > 0.0))
            throw std::invalid_argument("SgdRegressor: learning_rate must be > 0");
        if (!(options_.l2 >= 0.0))
            throw std::invalid_argument("SgdRegressor: l2 must be >= 0");
        if (!(options_.power_t >= 0.0))
            throw std::invalid_argument("SgdRegressor: power_t must be >= 0");
        if (options_.epochs == 0)
            throw std::invalid_argument("SgdRegressor: epochs must be >= 1");
    }

    void SgdRegressor::reset()
    {
        names_.clear();
        weights_.clear();
        mean_.clear();
        m2_.clear();
        bias_ = 0.0;
        seen_ = 0;
        updates_ = 0;
        ready_ = false;
        // options_ survives: it is configuration, not learned state.
    }

    bool SgdRegressor::is_ready() const
    {
        return ready_;
    }

    double SgdRegressor::current_learning_rate() const noexcept
    {
        if (options_.power_t == 0.0)
            return options_.learning_rate;
        return options_.learning_rate / std::pow(1.0 + static_cast<double>(updates_), options_.power_t);
    }

    void SgdRegressor::bind_schema(const FeatureVector &features)
    {
        if (features.values.empty())
            throw std::invalid_argument("SgdRegressor: cannot train on an empty feature vector");

        names_ = features.names;
        weights_.assign(features.values.size(), 0.0);
        mean_.assign(features.values.size(), 0.0);
        m2_.assign(features.values.size(), 0.0);
        bias_ = 0.0;
        seen_ = 0;
    }

    void SgdRegressor::require_schema(const FeatureVector &features) const
    {
        if (features.values.size() != weights_.size())
        {
            throw std::invalid_argument("SgdRegressor: expected " + std::to_string(weights_.size()) +
                                        " features but got " + std::to_string(features.values.size()));
        }

        // A nameless vector is accepted positionally, as LinearModel does.
        if (!names_.empty() && !features.names.empty() && features.names != names_)
        {
            throw std::invalid_argument("SgdRegressor: feature set mismatch: bound to [" +
                                        join_names(names_) + "] but got [" +
                                        join_names(features.names) + "]");
        }
    }

    void SgdRegressor::observe(const FeatureVector &features)
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

    double SgdRegressor::center_at(std::size_t j) const
    {
        return options_.standardize ? mean_[j] : 0.0;
    }

    double SgdRegressor::scale_at(std::size_t j) const
    {
        if (!options_.standardize || seen_ < 2)
            return 1.0;

        const double variance = m2_[j] / static_cast<double>(seen_ - 1);
        const double sigma = std::sqrt(variance);
        // A constant column has sigma 0. Dividing by 1 leaves it centered at exactly zero,
        // so it contributes nothing instead of producing an infinity.
        return (sigma > 1e-12) ? sigma : 1.0;
    }

    std::vector<double> SgdRegressor::to_z(const FeatureVector &features) const
    {
        std::vector<double> z(weights_.size(), 0.0);
        for (std::size_t j = 0; j < weights_.size(); ++j)
            z[j] = (features.values[j] - center_at(j)) / scale_at(j);
        return z;
    }

    double SgdRegressor::predict(const FeatureVector &features) const
    {
        if (!ready_)
            throw std::logic_error("SgdRegressor::predict() called before the first update");

        require_schema(features);

        const std::vector<double> z = to_z(features);
        double acc = bias_;
        for (std::size_t j = 0; j < weights_.size(); ++j)
            acc += weights_[j] * z[j];
        return acc;
    }

    void SgdRegressor::fit(std::span<const FeatureVector> X, std::span<const double> y)
    {
        if (X.size() != y.size())
        {
            throw std::invalid_argument("SgdRegressor::fit() got " + std::to_string(X.size()) +
                                        " feature vectors and " + std::to_string(y.size()) + " targets");
        }
        if (X.empty())
            throw std::invalid_argument("SgdRegressor::fit() needs at least one sample");

        // Sequential passes, no shuffling: see the class comment.
        for (std::size_t epoch = 0; epoch < options_.epochs; ++epoch)
        {
            for (std::size_t i = 0; i < X.size(); ++i)
                partial_fit(X[i], y[i]);
        }
    }

    void SgdRegressor::partial_fit(const FeatureVector &features, double target)
    {
        if (weights_.empty())
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
                throw std::invalid_argument("SgdRegressor::partial_fit(): " + column + " is not finite");
            }
        }
        if (!std::isfinite(target))
            throw std::invalid_argument("SgdRegressor::partial_fit(): target is not finite");

        // The sample's own moments go in before it is standardized. The features are known at
        // prediction time and only the target is not, so this adds no lookahead.
        observe(features);
        const std::vector<double> z = to_z(features);

        double prediction = bias_;
        for (std::size_t j = 0; j < weights_.size(); ++j)
            prediction += weights_[j] * z[j];

        const double error = prediction - target;
        if (!std::isfinite(error))
        {
            throw std::runtime_error("SgdRegressor diverged after " + std::to_string(updates_) +
                                     " updates: lower learning_rate (currently " +
                                     std::to_string(options_.learning_rate) + ")");
        }

        const double eta = current_learning_rate();
        for (std::size_t j = 0; j < weights_.size(); ++j)
            weights_[j] -= eta * (error * z[j] + options_.l2 * weights_[j]);
        // The bias is deliberately left out of the penalty: it carries the target's mean, and
        // shrinking it would bias every prediction toward zero.
        bias_ -= eta * error;

        ++updates_;
        ready_ = true;
    }

    LinearModel SgdRegressor::to_linear_model() const
    {
        if (!ready_)
            throw std::logic_error("SgdRegressor::to_linear_model() called before the first update");

        std::vector<double> folded(weights_.size(), 0.0);
        double bias = bias_;
        for (std::size_t j = 0; j < weights_.size(); ++j)
        {
            const double scale = scale_at(j);
            folded[j] = weights_[j] / scale;
            bias -= weights_[j] * center_at(j) / scale;
        }

        LinearModel model;
        if (names_.empty())
        {
            model.set_weights(std::move(folded), bias);
            return model;
        }

        std::vector<std::pair<std::string, double>> named;
        named.reserve(folded.size());
        for (std::size_t j = 0; j < folded.size(); ++j)
            named.emplace_back(names_[j], folded[j]);

        model.set_named_weights(std::move(named), bias);
        return model;
    }

    SgdTrainingSummary train_sgd_from_feature_rows(
        const std::vector<fin::indicators::FeatureRow> &rows,
        SgdOptions options)
    {
        if (rows.size() < 2)
            throw std::runtime_error("Need at least two feature rows to train an SGD model");

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

        SgdTrainingSummary summary{};
        summary.model = SgdRegressor(options);
        summary.model.fit(features, targets);
        summary.samples = samples;

        // Scored on the final weights, as the ridge path does, so the two MSEs mean the same
        // thing. It is not the running error accumulated during the passes, which would be
        // lower for the early samples and flatter for the late ones.
        double mse = 0.0;
        for (std::size_t i = 0; i < samples; ++i)
        {
            const double error = summary.model.predict(features[i]) - targets[i];
            mse += error * error;
        }
        summary.mse = mse / static_cast<double>(samples);
        return summary;
    }

} // namespace fin::ml
