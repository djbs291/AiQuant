#include "fin/ml/LinearTrainer.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <utility>

#include "fin/ml/FeatureVector.hpp"

namespace fin::ml
{
    namespace
    {
        using Matrix = std::vector<std::vector<double>>;

        Matrix make_matrix(std::size_t n)
        {
            return Matrix(n, std::vector<double>(n, 0.0));
        }

        bool solve_linear_system(Matrix &A, std::vector<double> &b)
        {
            const std::size_t n = A.size();
            for (std::size_t col = 0; col < n; ++col)
            {
                // Pivot selection
                std::size_t pivot = col;
                double max_val = std::fabs(A[pivot][col]);
                for (std::size_t row = col + 1; row < n; ++row)
                {
                    double v = std::fabs(A[row][col]);
                    if (v > max_val)
                    {
                        max_val = v;
                        pivot = row;
                    }
                }

                if (max_val < 1e-12)
                    return false;

                if (pivot != col)
                {
                    std::swap(A[pivot], A[col]);
                    std::swap(b[pivot], b[col]);
                }

                const double pivot_val = A[col][col];
                const double inv_pivot = 1.0 / pivot_val;

                for (std::size_t j = col; j < n; ++j)
                    A[col][j] *= inv_pivot;
                b[col] *= inv_pivot;

                for (std::size_t row = 0; row < n; ++row)
                {
                    if (row == col)
                        continue;
                    const double factor = A[row][col];
                    if (factor == 0.0)
                        continue;
                    for (std::size_t j = col; j < n; ++j)
                        A[row][j] -= factor * A[col][j];
                    b[row] -= factor * b[col];
                }
            }
            return true;
        }
    } // namespace

    LinearTrainingSummary train_linear_from_feature_rows(
        const std::vector<fin::indicators::FeatureRow> &rows,
        LinearTrainingOptions options)
    {
        if (rows.size() < 2)
            throw std::runtime_error("Need at least two feature rows to train linear model");

        // Build dataset using current features to predict next close delta.
        std::vector<FeatureVector> features;
        features.reserve(rows.size());
        for (const auto &row : rows)
            features.push_back(FeatureVector::from_feature_row(row));

        const std::size_t feature_count = features.front().values.size();
        const std::size_t augmented = feature_count + 1; // +1 for bias term
        const std::size_t samples = rows.size() - 1;

        // With a configurable feature set this is reachable: a wide set on a short file leaves
        // fewer samples than columns, which is underdetermined. Say so, rather than letting it
        // surface as an opaque "failed to solve normal equations" further down.
        if (samples < augmented)
        {
            throw std::runtime_error("Not enough samples to fit " + std::to_string(feature_count) +
                                     " features: need at least " + std::to_string(augmented) +
                                     " samples (features + bias) but have " + std::to_string(samples));
        }
        Matrix XtX = make_matrix(augmented);
        std::vector<double> Xty(augmented, 0.0);

        for (std::size_t i = 0; i < samples; ++i)
        {
            std::vector<double> aug(augmented, 0.0);
            for (std::size_t j = 0; j < feature_count; ++j)
                aug[j] = features[i].values[j];
            aug.back() = 1.0;

            const double target = rows[i + 1].close - rows[i].close;

            for (std::size_t r = 0; r < augmented; ++r)
            {
                for (std::size_t c = 0; c < augmented; ++c)
                    XtX[r][c] += aug[r] * aug[c];
                Xty[r] += aug[r] * target;
            }
        }

        const double ridge = options.ridge_lambda;
        for (std::size_t j = 0; j < feature_count; ++j)
            XtX[j][j] += ridge;

        if (!solve_linear_system(XtX, Xty))
            throw std::runtime_error("Failed to solve normal equations for linear model");

        const std::vector<double> solution = Xty;
        std::vector<std::pair<std::string, double>> named;
        named.reserve(feature_count);
        for (std::size_t j = 0; j < feature_count; ++j)
            named.emplace_back(features.front().names[j], solution[j]);
        const double bias = solution.back();

        LinearModel model;
        model.set_named_weights(named, bias);

        LinearTrainingSummary summary{};
        summary.model = std::move(model);
        summary.samples = samples;

        double mse = 0.0;
        for (std::size_t i = 0; i < samples; ++i)
        {
            double pred = bias;
            for (std::size_t j = 0; j < feature_count; ++j)
                pred += solution[j] * features[i].values[j];
            const double target = rows[i + 1].close - rows[i].close;
            const double err = pred - target;
            mse += err * err;
        }
        summary.mse = (samples > 0) ? (mse / static_cast<double>(samples)) : 0.0;
        return summary;
    }

    bool save_linear_model(const LinearModel &model, const std::string &path)
    {
        std::ofstream out(path);
        if (!out)
            return false;

        out << "# AiQuant LinearModel weights\n";

        const auto &named = model.named_weights();
        if (!named.empty())
        {
            // Record the feature set so a model cannot be silently applied to a different one.
            // It is a comment, so older readers skip it and older files still load.
            out << "# features:";
            for (std::size_t i = 0; i < named.size(); ++i)
                out << (i == 0 ? " " : ",") << named[i].first;
            out << "\n";
        }

        out << std::setprecision(12);
        out << "bias," << model.bias() << "\n";

        if (!named.empty())
        {
            for (const auto &[name, weight] : named)
                out << name << ',' << weight << "\n";
            return true;
        }

        const auto &weights = model.weights();
        for (std::size_t i = 0; i < weights.size(); ++i)
            out << "w" << i << ',' << weights[i] << "\n";
        return true;
    }
}
