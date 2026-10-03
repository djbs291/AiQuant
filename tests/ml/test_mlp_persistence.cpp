#include "catch2_compat.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "TestTempFiles.hpp"
#include "fin/ml/FeatureVector.hpp"
#include "fin/ml/LinearModel.hpp"
#include "fin/ml/LinearTrainer.hpp"
#include "fin/ml/MlpRegressor.hpp"

using fin::ml::FeatureVector;
using fin::ml::MlpOptions;
using fin::ml::MlpRegressor;

namespace
{
    FeatureVector vec(double x1, double x2)
    {
        FeatureVector fv;
        fv.names = {"x1", "x2"};
        fv.values = {x1, x2};
        return fv;
    }

    MlpRegressor trained_model()
    {
        MlpOptions options{};
        options.hidden_layers = {6, 4};
        options.learning_rate = 0.05;
        options.epochs = 200;
        options.seed = 17;

        std::vector<FeatureVector> X;
        std::vector<double> y;
        for (int a = 0; a < 7; ++a)
            for (int b = 0; b < 7; ++b)
            {
                const double x1 = -1.0 + 2.0 * a / 6.0;
                const double x2 = -1.0 + 2.0 * b / 6.0;
                X.push_back(vec(x1, x2));
                y.push_back(x1 * x2);
            }

        MlpRegressor model(options);
        model.fit(X, y);
        model.set_symbol("ABC");
        model.set_timeframe("M5");
        model.set_training_params({{"rsi", 10.0}, {"atr", 7.0}});
        return model;
    }
}

TEST_CASE("save_mlp_model / load_mlp_model round-trips predictions exactly", "[ml][mlp][persist]")
{
    const MlpRegressor model = trained_model();
    const auto path = test_files::temp_path("mlp_roundtrip_", ".csv");

    REQUIRE(fin::ml::save_mlp_model(model, path.string()));

    MlpRegressor loaded;
    REQUIRE(fin::ml::load_mlp_model(loaded, path.string()));

    // Metadata survives.
    REQUIRE(loaded.symbol() == "ABC");
    REQUIRE(loaded.timeframe() == "M5");
    REQUIRE(loaded.feature_names().size() == 2);
    REQUIRE(loaded.feature_names()[0] == "x1");
    REQUIRE(loaded.training_params().size() == 2);
    REQUIRE(loaded.training_params()[0].first == "rsi");
    REQUIRE(loaded.training_params()[0].second == Approx(10.0).margin(1e-12));
    REQUIRE(loaded.layer_sizes() == model.layer_sizes());

    // setprecision(17) round-trips a double exactly, so the reloaded model predicts bit for bit.
    for (int a = 0; a < 7; ++a)
        for (int b = 0; b < 7; ++b)
        {
            const auto fv = vec(-1.0 + 2.0 * a / 6.0, -1.0 + 2.0 * b / 6.0);
            REQUIRE(loaded.predict(fv) == model.predict(fv));
        }

    std::filesystem::remove(path);
}

TEST_CASE("peek_model_type tells an MLP file from a linear one", "[ml][mlp][persist]")
{
    const auto mlp_path = test_files::temp_path("mlp_type_", ".csv");
    REQUIRE(fin::ml::save_mlp_model(trained_model(), mlp_path.string()));

    std::string type;
    REQUIRE(fin::ml::peek_model_type(mlp_path.string(), type));
    REQUIRE(type == "mlp");

    // A linear model file has no "# type:" line, which reads as linear.
    fin::ml::LinearModel linear;
    linear.set_named_weights({{"close", 0.5}, {"rsi", -0.1}}, 0.2);
    const auto lin_path = test_files::temp_path("lin_type_", ".csv");
    REQUIRE(fin::ml::save_linear_model(linear, lin_path.string()));
    REQUIRE(fin::ml::peek_model_type(lin_path.string(), type));
    REQUIRE(type == "linear");

    std::filesystem::remove(mlp_path);
    std::filesystem::remove(lin_path);
}

TEST_CASE("load_model_file dispatches on the recorded type", "[ml][mlp][persist]")
{
    const MlpRegressor model = trained_model();
    const auto mlp_path = test_files::temp_path("mlp_dispatch_", ".csv");
    REQUIRE(fin::ml::save_mlp_model(model, mlp_path.string()));

    std::string error;
    auto as_mlp = fin::ml::load_model_file(mlp_path.string(), error);
    REQUIRE(as_mlp != nullptr);
    REQUIRE(error.empty());
    // It predicts through IModel, and the prediction matches the in-memory model.
    const auto fv = vec(0.3, -0.4);
    REQUIRE(as_mlp->predict(fv) == model.predict(fv));
    REQUIRE(as_mlp->symbol() == "ABC");

    // A linear file loads through the same entry point.
    fin::ml::LinearModel linear;
    linear.set_named_weights({{"x1", 1.0}, {"x2", 2.0}}, 0.5);
    const auto lin_path = test_files::temp_path("lin_dispatch_", ".csv");
    REQUIRE(fin::ml::save_linear_model(linear, lin_path.string()));
    auto as_linear = fin::ml::load_model_file(lin_path.string(), error);
    REQUIRE(as_linear != nullptr);
    REQUIRE(as_linear->predict(vec(1.0, 1.0)) == Approx(0.5 + 1.0 + 2.0).margin(1e-12));

    std::filesystem::remove(mlp_path);
    std::filesystem::remove(lin_path);
}

TEST_CASE("load_model_dir mixes linear and MLP models", "[ml][mlp][persist]")
{
    const auto dir = test_files::temp_path("model_dir_", "");
    std::filesystem::create_directories(dir);

    // One MLP model, saved under its symbol's name.
    MlpRegressor mlp = trained_model();
    mlp.set_symbol("ABC");
    REQUIRE(fin::ml::save_mlp_model(mlp, (dir / "ABC.csv").string()));

    // One linear model under another symbol's name.
    fin::ml::LinearModel linear;
    linear.set_named_weights({{"x1", 0.7}, {"x2", -0.2}}, 0.1);
    linear.set_symbol("XYZ");
    REQUIRE(fin::ml::save_linear_model(linear, (dir / "XYZ.csv").string()));

    std::unordered_map<std::string, std::shared_ptr<fin::ml::IModel>> models;
    std::string error;
    REQUIRE(fin::ml::load_model_dir(dir.string(), models, error));
    REQUIRE(models.size() == 2);
    REQUIRE(models.count("ABC") == 1);
    REQUIRE(models.count("XYZ") == 1);
    // Each predicts as its own kind.
    REQUIRE(models["ABC"]->predict(vec(0.2, 0.3)) == mlp.predict(vec(0.2, 0.3)));
    REQUIRE(models["XYZ"]->predict(vec(1.0, 1.0)) == Approx(0.1 + 0.7 - 0.2).margin(1e-12));

    std::filesystem::remove_all(dir);
}

TEST_CASE("load_model_dir refuses a model saved under the wrong symbol", "[ml][mlp][persist]")
{
    const auto dir = test_files::temp_path("model_dir_bad_", "");
    std::filesystem::create_directories(dir);

    MlpRegressor mlp = trained_model();
    mlp.set_symbol("ABC");
    // Saved as XYZ.csv though it was trained on ABC -- exactly what per-symbol models guard.
    REQUIRE(fin::ml::save_mlp_model(mlp, (dir / "XYZ.csv").string()));

    std::unordered_map<std::string, std::shared_ptr<fin::ml::IModel>> models;
    std::string error;
    REQUIRE_FALSE(fin::ml::load_model_dir(dir.string(), models, error));
    REQUIRE(error.find("ABC") != std::string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("load_mlp_model refuses a corrupt file", "[ml][mlp][persist]")
{
    // A well-formed header but a weight row with a non-numeric field.
    const test_files::TempFile bad(
        "mlp_corrupt_", ".csv",
        "# type: mlp\n"
        "activation,tanh\n"
        "standardize,1\n"
        "layers,2,3,1\n"
        "seen,10\n"
        "mean,0,0\n"
        "m2,0,0\n"
        "W0,0.1,0.2,0.3,0.4,0.5,xyz\n"
        "b0,0,0,0\n"
        "W1,0.1,0.2,0.3\n"
        "b1,0\n");

    MlpRegressor out;
    REQUIRE_FALSE(fin::ml::load_mlp_model(out, bad.path().string()));

    std::string error;
    REQUIRE(fin::ml::load_model_file(bad.path().string(), error) == nullptr);
    REQUIRE_FALSE(error.empty());
}
