#include "fin/ml/LinearModel.hpp"

#include <charconv>
#include <cctype>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fin/indicators/FeatureSpec.hpp"

namespace fin::ml
{
    namespace
    {
        std::string_view trim(std::string_view sv)
        {
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
                sv.remove_prefix(1);
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
                sv.remove_suffix(1);
            return sv;
        }

        std::optional<double> parse_double(std::string_view token)
        {
            double value = 0.0;
            const char *begin = token.data();
            const char *end = begin + token.size();
            auto [ptr, ec] = std::from_chars(begin, end, value);
            // Full consumption, as the INI and JSON parsers have always required: without the
            // `ptr == end` check "0.5abc" read as 0.5. And a non-finite weight is refused
            // outright — from_chars accepts "nan" by the standard's general format, and one
            // NaN weight turns every prediction this model ever makes into NaN.
            if (ec != std::errc{} || ptr != end || !std::isfinite(value))
                return std::nullopt;
            return value;
        }
    } // namespace

    LinearModel::LinearModel(std::vector<double> weights, double bias)
    {
        set_weights(std::move(weights), bias);
    }

    void LinearModel::reset()
    {
        bias_ = 0.0;
        weights_.clear();
        named_weights_.clear();
        ready_ = false;
    }

    bool LinearModel::is_ready() const
    {
        return ready_;
    }

    double LinearModel::predict(const FeatureVector &features) const
    {
        if (!ready_)
            throw std::logic_error("LinearModel::predict() called before loading weights");

        double acc = bias_;

        if (!named_weights_.empty())
        {
            for (const auto &[name, weight] : named_weights_)
            {
                if (auto value = features.value_of(name))
                    acc += weight * *value;
            }
            return acc;
        }

        if (weights_.empty())
            return acc;

        if (features.size() != weights_.size())
            throw std::invalid_argument("LinearModel::predict() feature dimension mismatch");

        for (std::size_t i = 0; i < weights_.size(); ++i)
            acc += weights_[i] * features.values[i];

        return acc;
    }

    void LinearModel::set_weights(std::vector<double> weights, double bias)
    {
        bias_ = bias;
        weights_ = std::move(weights);
        named_weights_.clear();
        ready_ = !weights_.empty();
    }

    void LinearModel::set_named_weights(std::vector<std::pair<std::string, double>> weights, double bias)
    {
        bias_ = bias;
        named_weights_ = std::move(weights);
        weights_.clear();
        ready_ = !named_weights_.empty();
    }

    bool LinearModel::load_from_file(const std::string &path)
    {
        std::ifstream in(path);
        if (!in)
            return false;

        std::vector<std::pair<std::string, double>> named;
        std::vector<std::string> feature_names;
        std::string symbol;
        std::vector<std::pair<std::string, double>> training_params;
        std::string timeframe;
        double bias = 0.0;
        bool bias_set = false;

        std::string line;
        while (std::getline(in, line))
        {
            std::string_view sv(line);
            sv = trim(sv);
            if (sv.empty() || sv.front() == '#')
            {
                // "# features: a,b,c" records the column order the model was trained on.
                constexpr std::string_view prefix = "# features:";
                if (sv.size() > prefix.size() && sv.substr(0, prefix.size()) == prefix)
                {
                    feature_names.clear();
                    std::string_view list = trim(sv.substr(prefix.size()));
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

                // "# symbol: ABC" records the instrument the model was trained on.
                constexpr std::string_view symbol_prefix = "# symbol:";
                if (sv.size() > symbol_prefix.size() && sv.substr(0, symbol_prefix.size()) == symbol_prefix)
                    symbol = std::string(trim(sv.substr(symbol_prefix.size())));

                // "# timeframe: M1" records the candles it was trained on.
                constexpr std::string_view timeframe_prefix = "# timeframe:";
                if (sv.size() > timeframe_prefix.size() && sv.substr(0, timeframe_prefix.size()) == timeframe_prefix)
                    timeframe = std::string(trim(sv.substr(timeframe_prefix.size())));

                // "# params: rsi=10,atr=7" records the periods the features were computed with.
                // Held to the same standard as a weight: the line is machine-written, so an
                // item that does not parse, an unknown key or an impossible value is a corrupt
                // file, and loading it would rebuild the features with the wrong periods.
                constexpr std::string_view params_prefix = "# params:";
                if (sv.size() >= params_prefix.size() && sv.substr(0, params_prefix.size()) == params_prefix)
                {
                    training_params.clear();
                    fin::indicators::FeatureParams scratch{};
                    std::string_view list = trim(sv.substr(params_prefix.size()));
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

            auto delim = sv.find_first_of(",;\t ");
            if (delim == std::string_view::npos)
                continue;

            std::string_view key = trim(sv.substr(0, delim));
            std::string_view value_token = trim(sv.substr(delim + 1));
            if (value_token.empty())
                continue;

            auto parsed = parse_double(value_token);
            if (!parsed)
            {
                // These files are machine-written by save_linear_model, so a value that does
                // not parse means the file is corrupt. Skipping the line would quietly drop a
                // feature's weight and leave a model that looks fine and predicts wrongly.
                return false;
            }

            if (key == "bias" || key == "intercept")
            {
                bias = *parsed;
                bias_set = true;
            }
            else
            {
                named.emplace_back(std::string(key), *parsed);
            }
        }

        if (named.empty())
            return false;

        set_named_weights(std::move(named), bias_set ? bias : 0.0);
        feature_names_ = std::move(feature_names);
        symbol_ = std::move(symbol);
        training_params_ = std::move(training_params);
        timeframe_ = std::move(timeframe);
        return ready_;
    }

    bool load_linear_model_dir(const std::string &dir,
                               std::unordered_map<std::string, std::shared_ptr<LinearModel>> &out,
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

        std::unordered_map<std::string, std::shared_ptr<LinearModel>> loaded;
        for (const auto &path : files)
        {
            const std::string symbol = path.stem().string();
            auto model = std::make_shared<LinearModel>();
            if (!model->load_from_file(path.string()))
            {
                error = "Failed to load model file " + path.string();
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

    void LinearModel::validate_schema(const FeatureVector &features) const
    {
        if (feature_names_.empty())
            return; // model file predates the schema line; nothing to check against

        const auto join = [](const std::vector<std::string> &names) {
            std::string out;
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                if (i > 0)
                    out += ", ";
                out += names[i];
            }
            return out;
        };

        if (features.names != feature_names_)
        {
            throw std::invalid_argument("Feature set mismatch: model was trained on [" +
                                        join(feature_names_) + "] but the input has [" +
                                        join(features.names) + "]");
        }
    }
}
