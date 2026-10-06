#include "fin/ml/MlpRegressor.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <memory>
#include <unordered_map>

#include "fin/ml/FeatureVector.hpp"
#include "fin/ml/LinearModel.hpp"
#include "fin/indicators/FeatureSpec.hpp"

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

        std::string_view trim(std::string_view sv)
        {
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
                sv.remove_prefix(1);
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
                sv.remove_suffix(1);
            return sv;
        }

        // Full consumption and finiteness, exactly as LinearModel::load_from_file: these files
        // are machine-written, so a token that does not fully parse, or a non-finite one, means
        // the file is corrupt.
        std::optional<double> parse_double(std::string_view token)
        {
            double value = 0.0;
            const char *begin = token.data();
            const char *end = begin + token.size();
            auto [ptr, ec] = std::from_chars(begin, end, value);
            if (ec != std::errc{} || ptr != end || !std::isfinite(value))
                return std::nullopt;
            return value;
        }

        std::optional<std::size_t> parse_size(std::string_view token)
        {
            std::size_t value = 0;
            const char *begin = token.data();
            const char *end = begin + token.size();
            auto [ptr, ec] = std::from_chars(begin, end, value);
            if (ec != std::errc{} || ptr != end)
                return std::nullopt;
            return value;
        }

        // Splits a "key,v0,v1,..." line into its key and the comma-separated fields after it.
        // Returns false when the line has no comma (no key/value separation).
        bool split_key_fields(std::string_view line, std::string_view &key,
                              std::vector<std::string_view> &fields)
        {
            const auto comma = line.find(',');
            if (comma == std::string_view::npos)
                return false;
            key = trim(line.substr(0, comma));
            fields.clear();
            std::string_view rest = line.substr(comma + 1);
            while (true)
            {
                const auto next = rest.find(',');
                const auto end = (next == std::string_view::npos) ? rest.size() : next;
                fields.push_back(trim(rest.substr(0, end)));
                if (next == std::string_view::npos)
                    break;
                rest.remove_prefix(next + 1);
            }
            return true;
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

    void MlpRegressor::validate_schema(const FeatureVector &features) const
    {
        require_schema(features);
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

    MlpRegressor MlpRegressor::from_state(MlpOptions options,
                                          std::vector<std::string> names,
                                          std::vector<std::size_t> layer_sizes,
                                          std::vector<std::vector<double>> weights,
                                          std::vector<std::vector<double>> biases,
                                          std::vector<double> mean,
                                          std::vector<double> m2,
                                          std::size_t seen)
    {
        MlpRegressor m(options); // validates the options (hidden layers, rate, l2, epochs)

        if (layer_sizes.size() < 2)
            throw std::invalid_argument("MlpRegressor::from_state: need at least an input and an output layer");
        if (layer_sizes.back() != 1)
            throw std::invalid_argument("MlpRegressor::from_state: the output layer must be a single unit");
        if (weights.size() + 1 != layer_sizes.size() || biases.size() != weights.size())
            throw std::invalid_argument("MlpRegressor::from_state: one weight matrix and bias vector per layer transition");
        for (std::size_t l = 0; l < weights.size(); ++l)
        {
            const std::size_t fan_in = layer_sizes[l];
            const std::size_t fan_out = layer_sizes[l + 1];
            if (fan_in == 0 || fan_out == 0)
                throw std::invalid_argument("MlpRegressor::from_state: a layer has zero width");
            if (weights[l].size() != fan_out * fan_in)
                throw std::invalid_argument("MlpRegressor::from_state: weight matrix " + std::to_string(l) +
                                            " has the wrong size");
            if (biases[l].size() != fan_out)
                throw std::invalid_argument("MlpRegressor::from_state: bias vector " + std::to_string(l) +
                                            " has the wrong size");
        }

        const std::size_t n_in = layer_sizes.front();
        if (mean.size() != n_in || m2.size() != n_in)
            throw std::invalid_argument("MlpRegressor::from_state: standardizer size does not match the input width");
        if (!names.empty() && names.size() != n_in)
            throw std::invalid_argument("MlpRegressor::from_state: feature count does not match the input width");

        m.names_ = std::move(names);
        m.layer_sizes_ = std::move(layer_sizes);
        m.weights_ = std::move(weights);
        m.biases_ = std::move(biases);
        m.mean_ = std::move(mean);
        m.m2_ = std::move(m2);
        m.seen_ = seen;
        // A loaded model is a frozen predictor; updates_ is informational only.
        m.updates_ = seen;
        m.ready_ = true;
        return m;
    }

    bool save_mlp_model(const MlpRegressor &model, const std::string &path)
    {
        if (!model.is_ready())
            return false;

        std::ofstream out(path);
        if (!out)
            return false;

        out << "# AiQuant MlpModel\n";
        out << "# type: mlp\n";

        const auto &names = model.feature_names();
        if (!names.empty())
        {
            out << "# features:";
            for (std::size_t i = 0; i < names.size(); ++i)
                out << (i == 0 ? " " : ",") << names[i];
            out << "\n";
        }
        if (!model.symbol().empty())
            out << "# symbol: " << model.symbol() << "\n";
        if (!model.timeframe().empty())
            out << "# timeframe: " << model.timeframe() << "\n";
        const auto &params = model.training_params();
        if (!params.empty())
        {
            out << "# params:";
            for (std::size_t i = 0; i < params.size(); ++i)
                out << (i == 0 ? " " : ",") << params[i].first << '=' << std::setprecision(17) << params[i].second;
            out << "\n";
        }

        out << "activation," << (model.options().activation == MlpActivation::Relu ? "relu" : "tanh") << "\n";
        out << "standardize," << (model.options().standardize ? 1 : 0) << "\n";

        out << "layers";
        for (std::size_t width : model.layer_sizes())
            out << ',' << width;
        out << "\n";

        out << "seen," << model.samples_seen() << "\n";

        // setprecision(17) round-trips an IEEE double exactly, so a reloaded model predicts
        // bit-for-bit as the trained one.
        out << std::setprecision(17);

        const auto write_row = [&out](std::string_view key, const std::vector<double> &values)
        {
            out << key;
            for (double v : values)
                out << ',' << v;
            out << "\n";
        };

        write_row("mean", model.standardizer_mean());
        write_row("m2", model.standardizer_m2());

        const auto &weights = model.weights();
        const auto &biases = model.biases();
        for (std::size_t l = 0; l < weights.size(); ++l)
        {
            write_row("W" + std::to_string(l), weights[l]);
            write_row("b" + std::to_string(l), biases[l]);
        }
        return static_cast<bool>(out);
    }

    bool load_mlp_model(MlpRegressor &out, const std::string &path)
    {
        std::ifstream in(path);
        if (!in)
            return false;

        std::vector<std::string> feature_names;
        std::string symbol;
        std::string timeframe;
        std::vector<std::pair<std::string, double>> training_params;

        MlpOptions options{};
        bool have_activation = false;
        bool have_standardize = false;
        std::vector<std::size_t> layer_sizes;
        std::optional<std::size_t> seen;
        std::vector<double> mean;
        std::vector<double> m2;
        // Weight and bias rows are keyed W0,b0,W1,b1,... We collect them in order and pair them
        // up at the end against the layer count the "layers" line implies.
        std::vector<std::vector<double>> weights;
        std::vector<std::vector<double>> biases;

        std::string line;
        while (std::getline(in, line))
        {
            std::string_view sv = trim(std::string_view(line));
            if (sv.empty())
                continue;

            if (sv.front() == '#')
            {
                constexpr std::string_view feat = "# features:";
                constexpr std::string_view sym = "# symbol:";
                constexpr std::string_view tf = "# timeframe:";
                constexpr std::string_view par = "# params:";
                if (sv.size() > feat.size() && sv.substr(0, feat.size()) == feat)
                {
                    feature_names.clear();
                    std::string_view list = trim(sv.substr(feat.size()));
                    while (!list.empty())
                    {
                        const auto comma = list.find(',');
                        const auto end = (comma == std::string_view::npos) ? list.size() : comma;
                        std::string_view item = trim(list.substr(0, end));
                        if (!item.empty())
                            feature_names.emplace_back(item);
                        if (comma == std::string_view::npos)
                            break;
                        list.remove_prefix(comma + 1);
                    }
                }
                else if (sv.size() > sym.size() && sv.substr(0, sym.size()) == sym)
                    symbol = std::string(trim(sv.substr(sym.size())));
                else if (sv.size() > tf.size() && sv.substr(0, tf.size()) == tf)
                    timeframe = std::string(trim(sv.substr(tf.size())));
                else if (sv.size() >= par.size() && sv.substr(0, par.size()) == par)
                {
                    training_params.clear();
                    fin::indicators::FeatureParams scratch{};
                    std::string_view list = trim(sv.substr(par.size()));
                    while (!list.empty())
                    {
                        const auto comma = list.find(',');
                        const auto end = (comma == std::string_view::npos) ? list.size() : comma;
                        const std::string_view item = trim(list.substr(0, end));
                        const auto eq = item.find('=');
                        if (eq == std::string_view::npos)
                            return false;
                        const std::string_view key = trim(item.substr(0, eq));
                        const auto value = parse_double(trim(item.substr(eq + 1)));
                        if (!value || !fin::indicators::set_feature_param(scratch, key, *value))
                            return false;
                        training_params.emplace_back(std::string(key), *value);
                        if (comma == std::string_view::npos)
                            break;
                        list.remove_prefix(comma + 1);
                    }
                }
                continue;
            }

            std::string_view key;
            std::vector<std::string_view> fields;
            if (!split_key_fields(sv, key, fields) || fields.empty())
                return false;

            if (key == "activation")
            {
                if (fields[0] == "tanh")
                    options.activation = MlpActivation::Tanh;
                else if (fields[0] == "relu")
                    options.activation = MlpActivation::Relu;
                else
                    return false;
                have_activation = true;
            }
            else if (key == "standardize")
            {
                if (fields[0] == "1")
                    options.standardize = true;
                else if (fields[0] == "0")
                    options.standardize = false;
                else
                    return false;
                have_standardize = true;
            }
            else if (key == "layers")
            {
                layer_sizes.clear();
                for (std::string_view f : fields)
                {
                    const auto v = parse_size(f);
                    if (!v)
                        return false;
                    layer_sizes.push_back(*v);
                }
            }
            else if (key == "seen")
            {
                const auto v = parse_size(fields[0]);
                if (!v)
                    return false;
                seen = *v;
            }
            else if (key == "mean" || key == "m2")
            {
                std::vector<double> row;
                row.reserve(fields.size());
                for (std::string_view f : fields)
                {
                    const auto v = parse_double(f);
                    if (!v)
                        return false;
                    row.push_back(*v);
                }
                (key == "mean" ? mean : m2) = std::move(row);
            }
            else if (!key.empty() && (key.front() == 'W' || key.front() == 'b'))
            {
                std::vector<double> row;
                row.reserve(fields.size());
                for (std::string_view f : fields)
                {
                    const auto v = parse_double(f);
                    if (!v)
                        return false;
                    row.push_back(*v);
                }
                (key.front() == 'W' ? weights : biases).push_back(std::move(row));
            }
            else
            {
                return false; // an unknown key means a file this loader does not understand
            }
        }

        if (!have_activation || !have_standardize || layer_sizes.size() < 2 || !seen)
            return false;
        if (weights.size() != biases.size() || weights.size() + 1 != layer_sizes.size())
            return false;

        // The hidden widths the options carry must match the architecture, so the validating
        // constructor inside from_state accepts them.
        options.hidden_layers.assign(layer_sizes.begin() + 1, layer_sizes.end() - 1);
        if (options.hidden_layers.empty())
            return false;

        try
        {
            MlpRegressor loaded = MlpRegressor::from_state(options, std::move(feature_names),
                                                           std::move(layer_sizes), std::move(weights),
                                                           std::move(biases), std::move(mean),
                                                           std::move(m2), *seen);
            loaded.set_symbol(std::move(symbol));
            loaded.set_timeframe(std::move(timeframe));
            loaded.set_training_params(std::move(training_params));
            out = std::move(loaded);
            return true;
        }
        catch (const std::invalid_argument &)
        {
            return false;
        }
    }

    bool peek_model_type(const std::string &path, std::string &type_out)
    {
        std::ifstream in(path);
        if (!in)
            return false;

        type_out = "linear"; // no "# type:" line at all is a pre-existing linear file
        std::string line;
        while (std::getline(in, line))
        {
            std::string_view sv = trim(std::string_view(line));
            if (sv.empty())
                continue;
            if (sv.front() != '#')
                break; // the header is over; no type line means linear
            constexpr std::string_view prefix = "# type:";
            if (sv.size() > prefix.size() && sv.substr(0, prefix.size()) == prefix)
            {
                type_out = std::string(trim(sv.substr(prefix.size())));
                return true;
            }
        }
        return true;
    }

    std::shared_ptr<IModel> load_model_file(const std::string &path, std::string &error)
    {
        std::string type;
        if (!peek_model_type(path, type))
        {
            error = "Failed to open model file: " + path;
            return nullptr;
        }

        if (type == "mlp")
        {
            auto model = std::make_shared<MlpRegressor>();
            if (!load_mlp_model(*model, path))
            {
                error = "Failed to load MLP model (corrupt or unreadable): " + path;
                return nullptr;
            }
            return model;
        }

        if (type != "linear")
        {
            error = "Unknown model type '" + type + "' in " + path;
            return nullptr;
        }

        auto model = std::make_shared<LinearModel>();
        if (!model->load_from_file(path))
        {
            error = "Failed to load linear model (corrupt or unreadable): " + path;
            return nullptr;
        }
        return model;
    }

    bool load_model_dir(const std::string &dir,
                        std::unordered_map<std::string, std::shared_ptr<IModel>> &out,
                        std::string &error)
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        if (!fs::is_directory(dir, ec))
        {
            error = "Model directory not found: " + dir;
            return false;
        }

        std::vector<fs::path> files;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        {
            const fs::path &path = it->path();
            const std::string name = path.filename().string();
            std::error_code type_ec;
            if (name.empty() || name.front() == '.' || path.extension() != ".csv" ||
                !it->is_regular_file(type_ec))
                continue;
            files.push_back(path);
        }
        if (ec)
        {
            error = "Cannot read model directory " + dir + ": " + ec.message();
            return false;
        }

        // Sorted, so which bad file gets reported does not depend on the directory's order.
        std::sort(files.begin(), files.end());

        std::unordered_map<std::string, std::shared_ptr<IModel>> loaded;
        for (const auto &path : files)
        {
            const std::string symbol = path.stem().string();
            std::string file_error;
            auto model = load_model_file(path.string(), file_error);
            if (!model)
            {
                error = file_error;
                return false;
            }
            if (!model->symbol().empty() && model->symbol() != symbol)
            {
                error = "Model file " + path.string() + " was trained on '" + model->symbol() +
                        "', not '" + symbol + "'";
                return false;
            }
            loaded.emplace(symbol, std::move(model));
        }

        if (loaded.empty())
        {
            error = "No model files (*.csv) in " + dir;
            return false;
        }

        out = std::move(loaded);
        return true;
    }

} // namespace fin::ml
