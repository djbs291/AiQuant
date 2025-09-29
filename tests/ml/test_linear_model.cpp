#include "catch2_compat.hpp"

#include <filesystem>
#include <fstream>
#include "fin/ml/FeatureVector.hpp"
#include "fin/ml/LinearModel.hpp"
#include "fin/indicators/FeatureBus.hpp"

using fin::indicators::FeatureRow;
using fin::ml::FeatureVector;
using fin::ml::LinearModel;

TEST_CASE("LinearModel positional weights", "[ml][linear]")
{
    FeatureRow row{
        fin::core::Timestamp{},
        100.0, 101.0, 60.0,
        1.5, 1.2, 0.3};

    auto fv = FeatureVector::from_feature_row(row);

    LinearModel model;
    model.set_weights({0.01, -0.02, 0.03, 0.5, -0.1, 0.2}, -1.0);

    REQUIRE(model.is_ready());
    double pred = model.predict(fv);
    REQUIRE(pred == Approx(0.47).margin(1e-6));
}

TEST_CASE("LinearModel named weights", "[ml][linear]")
{
    FeatureRow row{
        fin::core::Timestamp{},
        100.0, 101.0, 60.0,
        1.5, 1.2, 0.3};

    auto fv = FeatureVector::from_feature_row(row);

    LinearModel model;
    model.set_named_weights({{"macd", 1.0}, {"close", 0.05}}, 0.1);

    REQUIRE(model.is_ready());

    double pred = model.predict(fv);
    REQUIRE(pred == Approx(6.6).margin(1e-6));
}

TEST_CASE("LinearModel loads from file", "[ml][linear]")
{
    const auto path = std::filesystem::temp_directory_path() / "aiquant_linear_model_test.csv";
    {
        std::ofstream out(path);
        out << "bias,0.2\n";
        out << "close,0.1\n";
        out << "ema_fast,-0.05\n";
    }

    LinearModel model;
    REQUIRE(model.load_from_file(path.string()));
    REQUIRE(model.is_ready());

    FeatureRow row{
        fin::core::Timestamp{},
        100.0, 98.0, 55.0,
        1.0, 0.8, 0.2};

    auto fv = FeatureVector::from_feature_row(row);
    double expected = 0.2 + 0.1 * 100.0 + (-0.05) * 98.0; // rsi/macd weights omitted => 0
    double pred = model.predict(fv);
    REQUIRE(pred == Approx(expected).margin(1e-6));
    std::filesystem::remove(path);
}
