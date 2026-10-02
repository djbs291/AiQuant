#include "catch2_compat.hpp"

#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

#include "fin/indicators/FeatureBus.hpp"
#include "fin/ml/FeatureVector.hpp"
#include "fin/ml/MlpRegressor.hpp"

using fin::ml::FeatureVector;
using fin::ml::MlpActivation;
using fin::ml::MlpOptions;
using fin::ml::MlpRegressor;

namespace
{
    // Two columns three orders of magnitude apart, the situation the standardizer exists for.
    FeatureVector scaled_vector(int i)
    {
        FeatureVector fv;
        fv.names = {"close", "macd"};
        fv.values = {100.0 + 0.1 * static_cast<double>(i),
                     0.01 * static_cast<double>((i % 7) - 3)};
        return fv;
    }

    double linear_target(const FeatureVector &fv)
    {
        return 0.5 + 0.3 * fv.values[0] - 5.0 * fv.values[1];
    }

    // A grid of the unit square, with a feature vector and its product target. The product is
    // not linearly representable: the best linear fit over this symmetric grid is y = 0.
    void product_grid(std::vector<FeatureVector> &X, std::vector<double> &y, int n)
    {
        X.clear();
        y.clear();
        for (int a = 0; a < n; ++a)
        {
            for (int b = 0; b < n; ++b)
            {
                const double x1 = -1.0 + 2.0 * static_cast<double>(a) / static_cast<double>(n - 1);
                const double x2 = -1.0 + 2.0 * static_cast<double>(b) / static_cast<double>(n - 1);
                FeatureVector fv;
                fv.names = {"x1", "x2"};
                fv.values = {x1, x2};
                X.push_back(fv);
                y.push_back(x1 * x2);
            }
        }
    }

    double rmse(const MlpRegressor &model, const std::vector<FeatureVector> &X,
               const std::vector<double> &y)
    {
        double sse = 0.0;
        for (std::size_t i = 0; i < X.size(); ++i)
        {
            const double err = model.predict(X[i]) - y[i];
            sse += err * err;
        }
        return std::sqrt(sse / static_cast<double>(X.size()));
    }
}

TEST_CASE("MlpRegressor fits a linear relation across unequal feature scales", "[ml][mlp]")
{
    std::vector<FeatureVector> X;
    std::vector<double> y;
    for (int i = 0; i < 60; ++i)
    {
        X.push_back(scaled_vector(i));
        y.push_back(linear_target(X.back()));
    }

    MlpOptions options{};
    options.hidden_layers = {8};
    options.learning_rate = 0.02;
    options.epochs = 400;

    MlpRegressor model(options);
    model.fit(X, y);

    REQUIRE(model.is_ready());
    REQUIRE(model.updates() == X.size() * options.epochs);
    // Input, one hidden layer, scalar output.
    REQUIRE(model.layer_sizes().size() == 3);
    REQUIRE(model.layer_sizes().front() == 2);
    REQUIRE(model.layer_sizes().back() == 1);

    REQUIRE(rmse(model, X, y) < 0.05);
}

TEST_CASE("MlpRegressor fits a nonlinear relation a linear model cannot", "[ml][mlp]")
{
    std::vector<FeatureVector> X;
    std::vector<double> y;
    product_grid(X, y, 8);

    // The linear floor: over this symmetric grid the best linear fit is y = 0, so its RMSE is
    // the standard deviation of the targets. The MLP must beat it comfortably.
    double mean = 0.0;
    for (double v : y)
        mean += v;
    mean /= static_cast<double>(y.size());
    double var = 0.0;
    for (double v : y)
        var += (v - mean) * (v - mean);
    const double linear_floor = std::sqrt(var / static_cast<double>(y.size()));

    MlpOptions options{};
    options.hidden_layers = {8};
    options.learning_rate = 0.05;
    options.epochs = 600;
    options.activation = MlpActivation::Tanh;

    MlpRegressor model(options);
    model.fit(X, y);

    const double mlp_rmse = rmse(model, X, y);
    REQUIRE(linear_floor > 0.2);      // the product really is hard to fit linearly
    REQUIRE(mlp_rmse < 0.5 * linear_floor);
    REQUIRE(mlp_rmse < 0.1);
}

TEST_CASE("MlpRegressor partial_fit reduces the error on a sample", "[ml][mlp]")
{
    MlpOptions options{};
    options.hidden_layers = {6};
    options.learning_rate = 0.05;

    MlpRegressor model(options);

    FeatureVector fv;
    fv.names = {"a", "b"};
    fv.values = {0.4, -0.7};
    const double target = 0.9;

    model.partial_fit(fv, target);
    REQUIRE(model.is_ready());
    const double first = std::fabs(model.predict(fv) - target);

    for (int i = 0; i < 200; ++i)
        model.partial_fit(fv, target);
    const double later = std::fabs(model.predict(fv) - target);

    REQUIRE(later < first);
    REQUIRE(later < 1e-3);
}

TEST_CASE("MlpRegressor predict before the first update throws", "[ml][mlp]")
{
    MlpRegressor model(MlpOptions{});
    FeatureVector fv;
    fv.names = {"a"};
    fv.values = {1.0};

    bool threw = false;
    try
    {
        (void)model.predict(fv);
    }
    catch (const std::logic_error &)
    {
        threw = true;
    }
    REQUIRE(threw);
}

TEST_CASE("MlpRegressor refuses a feature set it was not bound to", "[ml][mlp]")
{
    MlpOptions options{};
    options.hidden_layers = {4};
    MlpRegressor model(options);

    FeatureVector bound;
    bound.names = {"a", "b"};
    bound.values = {0.1, 0.2};
    model.partial_fit(bound, 0.3);

    FeatureVector wrong_size;
    wrong_size.names = {"a"};
    wrong_size.values = {0.1};

    bool threw_size = false;
    try
    {
        (void)model.predict(wrong_size);
    }
    catch (const std::invalid_argument &)
    {
        threw_size = true;
    }
    REQUIRE(threw_size);

    FeatureVector wrong_names;
    wrong_names.names = {"a", "c"};
    wrong_names.values = {0.1, 0.2};

    bool threw_names = false;
    try
    {
        (void)model.predict(wrong_names);
    }
    catch (const std::invalid_argument &)
    {
        threw_names = true;
    }
    REQUIRE(threw_names);
}

TEST_CASE("MlpRegressor training is reproducible from the seed", "[ml][mlp]")
{
    std::vector<FeatureVector> X;
    std::vector<double> y;
    product_grid(X, y, 6);

    MlpOptions options{};
    options.hidden_layers = {5};
    options.epochs = 50;
    options.seed = 123;

    MlpRegressor a(options);
    MlpRegressor b(options);
    a.fit(X, y);
    b.fit(X, y);

    // Same seed, same data: bit-for-bit identical predictions.
    for (const auto &fv : X)
        REQUIRE(a.predict(fv) == Approx(b.predict(fv)).margin(1e-12));

    // A different seed starts from different weights, so it lands somewhere else.
    MlpOptions other = options;
    other.seed = 999;
    MlpRegressor c(other);
    c.fit(X, y);
    REQUIRE(std::fabs(a.predict(X.front()) - c.predict(X.front())) > 1e-9);
}

TEST_CASE("MlpRegressor rejects a degenerate architecture", "[ml][mlp]")
{
    const auto refused = [](const MlpOptions &opts)
    {
        try
        {
            MlpRegressor model(opts);
        }
        catch (const std::invalid_argument &)
        {
            return true;
        }
        return false;
    };

    MlpOptions no_hidden{};
    no_hidden.hidden_layers = {};
    REQUIRE(refused(no_hidden));

    MlpOptions zero_width{};
    zero_width.hidden_layers = {4, 0};
    REQUIRE(refused(zero_width));

    MlpOptions bad_rate{};
    bad_rate.learning_rate = 0.0;
    REQUIRE(refused(bad_rate));

    MlpOptions bad_l2{};
    bad_l2.l2 = -1.0;
    REQUIRE(refused(bad_l2));

    MlpOptions no_epochs{};
    no_epochs.epochs = 0;
    REQUIRE(refused(no_epochs));
}

TEST_CASE("train_mlp_from_feature_rows fits the next-close delta", "[ml][mlp]")
{
    auto schema = std::make_shared<fin::indicators::FeatureSchema>();
    schema->names = {"close", "ema"};

    std::vector<fin::indicators::FeatureRow> rows;
    for (int i = 0; i < 40; ++i)
    {
        fin::indicators::FeatureRow row;
        row.close = 100.0 + static_cast<double>(i);
        row.values = {row.close, 100.0 + 0.9 * static_cast<double>(i)};
        row.schema = schema;
        rows.push_back(row);
    }

    MlpOptions options{};
    options.hidden_layers = {6};
    options.learning_rate = 0.02;
    options.epochs = 300;

    auto summary = fin::ml::train_mlp_from_feature_rows(rows, options);
    REQUIRE(summary.samples == rows.size() - 1);
    REQUIRE(summary.model.is_ready());
    // The delta is a constant +1 per step; a fitted model tracks it closely.
    REQUIRE(summary.mse < 0.05);
}
