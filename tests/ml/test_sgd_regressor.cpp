#include "catch2_compat.hpp"

#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "TestTempFiles.hpp"
#include "fin/ml/FeatureVector.hpp"
#include "fin/ml/LinearModel.hpp"
#include "fin/ml/LinearTrainer.hpp"
#include "fin/ml/SgdRegressor.hpp"

using fin::ml::FeatureVector;
using fin::ml::SgdOptions;
using fin::ml::SgdRegressor;

namespace
{
    // Two columns three orders of magnitude apart, which is the situation the standardizer
    // exists for: `close` is ~100 while `macd` is ~0.01.
    FeatureVector make_vector(int i)
    {
        FeatureVector fv;
        fv.names = {"close", "macd"};
        fv.values = {100.0 + 0.1 * static_cast<double>(i),
                     0.01 * static_cast<double>((i % 7) - 3)};
        return fv;
    }

    constexpr double kBias = 0.5;
    constexpr double kWeightClose = 0.3;
    constexpr double kWeightMacd = -5.0;

    double target_for(const FeatureVector &fv)
    {
        return kBias + kWeightClose * fv.values[0] + kWeightMacd * fv.values[1];
    }

    SgdOptions test_options()
    {
        SgdOptions options{};
        options.learning_rate = 0.05;
        options.l2 = 1e-9;
        options.epochs = 300;
        options.power_t = 0.25;
        return options;
    }
}

TEST_CASE("SgdRegressor fits a linear relation across unequal feature scales", "[ml][sgd]")
{
    std::vector<FeatureVector> X;
    std::vector<double> y;
    for (int i = 0; i < 60; ++i)
    {
        X.push_back(make_vector(i));
        y.push_back(target_for(X.back()));
    }

    SgdRegressor model(test_options());
    model.fit(X, y);

    REQUIRE(model.is_ready());
    REQUIRE(model.updates() == X.size() * test_options().epochs);

    double worst = 0.0;
    for (std::size_t i = 0; i < X.size(); ++i)
        worst = std::max(worst, std::fabs(model.predict(X[i]) - y[i]));
    REQUIRE(worst < 0.01);
}

TEST_CASE("SgdRegressor without standardization cannot follow the same data", "[ml][sgd]")
{
    // The point of the previous test: with raw features one learning rate has to serve a
    // column of ~100 and a column of ~0.01, and the first gradient step already overshoots.
    std::vector<FeatureVector> X;
    std::vector<double> y;
    for (int i = 0; i < 60; ++i)
    {
        X.push_back(make_vector(i));
        y.push_back(target_for(X.back()));
    }

    SgdOptions raw = test_options();
    raw.standardize = false;
    raw.epochs = 1;

    SgdRegressor model(raw);
    bool diverged = false;
    try
    {
        model.fit(X, y);
        // If it survived, it certainly did not fit: report the error instead of the throw.
        diverged = std::fabs(model.predict(X.front()) - y.front()) > 1.0;
    }
    catch (const std::runtime_error &)
    {
        diverged = true; // the guard in partial_fit fired
    }
    REQUIRE(diverged);
}

TEST_CASE("SgdRegressor exports an equivalent LinearModel", "[ml][sgd]")
{
    std::vector<FeatureVector> X;
    std::vector<double> y;
    for (int i = 0; i < 40; ++i)
    {
        X.push_back(make_vector(i));
        y.push_back(target_for(X.back()));
    }

    SgdRegressor model(test_options());
    model.fit(X, y);

    // Folding the running mean and sigma into the weights has to be exact, because this is
    // the only form the model is ever persisted or served in.
    const fin::ml::LinearModel folded = model.to_linear_model();
    for (const auto &fv : X)
        REQUIRE(folded.predict(fv) == Approx(model.predict(fv)).margin(1e-9));

    const auto path = test_files::temp_path("aiquant_sgd_model_", ".csv");
    REQUIRE(fin::ml::save_linear_model(folded, path.string()));

    fin::ml::LinearModel reloaded;
    REQUIRE(reloaded.load_from_file(path.string()));
    const std::vector<std::string> expected_names = {"close", "macd"};
    REQUIRE(reloaded.feature_names() == expected_names);
    REQUIRE(reloaded.predict(X.front()) == Approx(model.predict(X.front())).margin(1e-6));

    std::filesystem::remove(path);
}

TEST_CASE("SgdRegressor partial_fit tracks a relation that changes", "[ml][sgd]")
{
    // The reason online learning is worth having: the frozen model keeps predicting the old
    // regime, the online one follows the new one.
    std::vector<FeatureVector> first;
    std::vector<double> first_y;
    for (int i = 0; i < 120; ++i)
    {
        first.push_back(make_vector(i));
        first_y.push_back(target_for(first.back()));
    }

    SgdRegressor online(test_options());
    online.fit(first, first_y);
    const SgdRegressor frozen = online;

    double sse_frozen = 0.0;
    double sse_online = 0.0;
    for (int i = 120; i < 320; ++i)
    {
        const FeatureVector fv = make_vector(i);
        // The sign of the close coefficient flips: the same features now mean the opposite.
        const double shifted = kBias - kWeightClose * fv.values[0] + kWeightMacd * fv.values[1];

        const double err_frozen = frozen.predict(fv) - shifted;
        sse_frozen += err_frozen * err_frozen;

        // Prequential: score the sample before learning from it, so nothing is graded on
        // data it has already seen.
        const double err_online = online.predict(fv) - shifted;
        sse_online += err_online * err_online;

        online.partial_fit(fv, shifted);
    }

    REQUIRE(sse_online < sse_frozen * 0.5);
    REQUIRE(online.updates() > frozen.updates());
}

TEST_CASE("SgdRegressor implements the IModel training hooks", "[ml][sgd]")
{
    // Issue 10 in docs/ProjectStatus.md: until now every IModel threw logic_error from both.
    SgdRegressor concrete(test_options());
    fin::ml::IModel &model = concrete;

    std::vector<FeatureVector> X;
    std::vector<double> y;
    for (int i = 0; i < 20; ++i)
    {
        X.push_back(make_vector(i));
        y.push_back(target_for(X.back()));
    }

    model.fit(X, y);
    REQUIRE(model.is_ready());

    const FeatureVector next = make_vector(20);
    model.partial_fit(next, target_for(next));
    REQUIRE(concrete.updates() == X.size() * test_options().epochs + 1);

    model.reset();
    REQUIRE_FALSE(model.is_ready());
    REQUIRE(concrete.updates() == 0);
}

TEST_CASE("SgdRegressor refuses a feature set it was not bound to", "[ml][sgd]")
{
    SgdRegressor model(test_options());
    const FeatureVector bound = make_vector(0);
    model.partial_fit(bound, target_for(bound));

    FeatureVector renamed;
    renamed.names = {"close", "rsi"};
    renamed.values = {100.0, 55.0};

    bool threw = false;
    try
    {
        model.partial_fit(renamed, 0.1);
    }
    catch (const std::invalid_argument &)
    {
        threw = true;
    }
    REQUIRE(threw);

    FeatureVector shorter;
    shorter.names = {"close"};
    shorter.values = {100.0};

    threw = false;
    try
    {
        static_cast<void>(model.predict(shorter));
    }
    catch (const std::invalid_argument &)
    {
        threw = true;
    }
    REQUIRE(threw);
}

TEST_CASE("SgdRegressor rejects unusable options and premature predictions", "[ml][sgd]")
{
    bool threw = false;
    try
    {
        SgdOptions bad{};
        bad.learning_rate = 0.0;
        SgdRegressor model(bad);
    }
    catch (const std::invalid_argument &)
    {
        threw = true;
    }
    REQUIRE(threw);

    threw = false;
    try
    {
        SgdOptions bad{};
        bad.epochs = 0; // would "train" without ever updating a weight
        SgdRegressor model(bad);
    }
    catch (const std::invalid_argument &)
    {
        threw = true;
    }
    REQUIRE(threw);

    SgdRegressor fresh(test_options());
    threw = false;
    try
    {
        static_cast<void>(fresh.predict(make_vector(0)));
    }
    catch (const std::logic_error &)
    {
        threw = true;
    }
    REQUIRE(threw);

    threw = false;
    try
    {
        static_cast<void>(fresh.to_linear_model());
    }
    catch (const std::logic_error &)
    {
        threw = true;
    }
    REQUIRE(threw);
}

TEST_CASE("SgdRegressor rejects a non-finite sample before it poisons the moments", "[ml][sgd]")
{
    SgdRegressor model(test_options());
    const FeatureVector good = make_vector(0);
    model.partial_fit(good, target_for(good));
    const double before = model.predict(good);

    FeatureVector nan_row = make_vector(1);
    nan_row.values[1] = std::nan("");

    bool threw = false;
    try
    {
        model.partial_fit(nan_row, 0.1);
    }
    catch (const std::invalid_argument &)
    {
        threw = true;
    }
    REQUIRE(threw);

    // The rejected sample left no trace: the model still predicts what it did before.
    REQUIRE(model.predict(good) == Approx(before).margin(1e-12));
}
