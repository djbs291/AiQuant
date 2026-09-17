#include "catch2_compat.hpp"

#include <filesystem>
#include <fstream>
#include <vector>
#include "fin/ml/FeatureVector.hpp"
#include "fin/ml/LinearModel.hpp"
#include "fin/ml/LinearTrainer.hpp"
#include "fin/indicators/FeatureBus.hpp"

using fin::indicators::FeatureRow;
using fin::ml::FeatureVector;
using fin::ml::LinearModel;

namespace
{
    // Rows carry their schema now, so build them through the default six rather than by
    // aggregate initialisation.
    std::shared_ptr<const fin::indicators::FeatureSchema> default_schema()
    {
        static const std::shared_ptr<const fin::indicators::FeatureSchema> schema = [] {
            auto s = std::make_shared<fin::indicators::FeatureSchema>();
            s->names = fin::indicators::default_feature_names();
            return s;
        }();
        return schema;
    }

    FeatureRow make_feature_row(double close, double ema_fast, double rsi,
                                double macd, double macd_signal, double macd_hist)
    {
        FeatureRow row;
        row.ts = fin::core::Timestamp{};
        row.close = close;
        row.values = {close, ema_fast, rsi, macd, macd_signal, macd_hist};
        row.schema = default_schema();
        return row;
    }
}

TEST_CASE("LinearModel positional weights", "[ml][linear]")
{
    FeatureRow row = make_feature_row(100.0, 101.0, 60.0, 1.5, 1.2, 0.3);

    auto fv = FeatureVector::from_feature_row(row);

    LinearModel model;
    model.set_weights({0.01, -0.02, 0.03, 0.5, -0.1, 0.2}, -1.0);

    REQUIRE(model.is_ready());
    double pred = model.predict(fv);
    REQUIRE(pred == Approx(0.47).margin(1e-6));
}

TEST_CASE("LinearModel named weights", "[ml][linear]")
{
    FeatureRow row = make_feature_row(100.0, 101.0, 60.0, 1.5, 1.2, 0.3);

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

    FeatureRow row = make_feature_row(100.0, 98.0, 55.0, 1.0, 0.8, 0.2);

    auto fv = FeatureVector::from_feature_row(row);
    double expected = 0.2 + 0.1 * 100.0 + (-0.05) * 98.0; // rsi/macd weights omitted => 0
    double pred = model.predict(fv);
    REQUIRE(pred == Approx(expected).margin(1e-6));
    std::filesystem::remove(path);
}

TEST_CASE("LinearTrainer recovers known weights", "[ml][linear]")
{
    const double bias = 0.25;
    const std::vector<double> weights = {0.01, -0.015, 0.05, 0.08, -0.03, 0.02};

    std::vector<FeatureRow> rows;
    double close = 100.0;
    const int samples = 16;

    auto make_row = [](int idx, double cls)
    {
        return make_feature_row(cls,
                                10.0 + 0.15 * cls + 0.7 * idx,
                                35.0 + 1.1 * idx * idx,
                                0.6 * idx + 0.01 * cls,
                                0.2 * idx * idx,
                                0.05 * idx + 0.02 * idx * idx);
    };

    for (int i = 0; i < samples; ++i)
    {
        FeatureRow row = make_row(i, close);
        rows.push_back(row);

        double delta = bias;
        for (std::size_t j = 0; j < weights.size(); ++j)
            delta += weights[j] * row.values[j];
        close += delta;
    }
    rows.push_back(make_row(samples, close));

    auto summary = fin::ml::train_linear_from_feature_rows(rows);
    REQUIRE(summary.samples == static_cast<std::size_t>(samples));
    REQUIRE(summary.mse < 1e-4);

    auto fv0 = FeatureVector::from_feature_row(rows.front());
    double expected = bias;
    for (std::size_t j = 0; j < weights.size(); ++j)
        expected += weights[j] * rows.front().values[j];
    double pred = summary.model.predict(fv0);
    REQUIRE(pred == Approx(expected).margin(1e-3));
}

TEST_CASE("LinearTrainer saves and reloads models", "[ml][linear]")
{
    std::vector<FeatureRow> rows;
    double close = 50.0;
    const double bias = -0.1;
    const std::vector<double> weights = {0.02, 0.01, -0.03, 0.04, 0.02, -0.01};

    auto make_row = [](int idx, double cls)
    {
        return make_feature_row(cls,
                                5.0 + 0.08 * cls + 0.9 * idx,
                                25.0 + 0.7 * idx * idx,
                                0.4 * idx + 0.02 * cls,
                                0.1 * idx * idx,
                                -0.03 * idx + 0.015 * idx * idx);
    };

    for (int i = 0; i < 12; ++i)
    {
        FeatureRow row = make_row(i, close);
        rows.push_back(row);

        double delta = bias;
        for (std::size_t j = 0; j < weights.size(); ++j)
            delta += weights[j] * row.values[j];
        close += delta;
    }
    rows.push_back(make_row(12, close));

    auto summary = fin::ml::train_linear_from_feature_rows(rows);
    const auto temp_path = std::filesystem::temp_directory_path() / "aiquant_linear_model_tmp.csv";
    REQUIRE(fin::ml::save_linear_model(summary.model, temp_path.string()));

    LinearModel loaded;
    REQUIRE(loaded.load_from_file(temp_path.string()));

    auto fv = FeatureVector::from_feature_row(rows.front());
    double expected = bias;
    for (std::size_t j = 0; j < weights.size(); ++j)
        expected += weights[j] * rows.front().values[j];
    double pred = loaded.predict(fv);
    REQUIRE(pred == Approx(expected).margin(1e-3));

    std::filesystem::remove(temp_path);
}
