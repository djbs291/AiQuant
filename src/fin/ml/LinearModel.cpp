#include "fin/ml/LinearModel.hpp"

#include <charconv>
#include <cctype>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

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
            if (auto [ptr, ec] = std::from_chars(begin, end, value); ec == std::errc{})
                return value;
            return std::nullopt;
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
        double bias = 0.0;
        bool bias_set = false;

        std::string line;
        while (std::getline(in, line))
        {
            std::string_view sv(line);
            sv = trim(sv);
            if (sv.empty() || sv.front() == '#')
                continue;

            auto delim = sv.find_first_of(",;\t ");
            if (delim == std::string_view::npos)
                continue;

            std::string_view key = trim(sv.substr(0, delim));
            std::string_view value_token = trim(sv.substr(delim + 1));
            if (value_token.empty())
                continue;

            auto parsed = parse_double(value_token);
            if (!parsed)
                continue;

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
        return ready_;
    }
}
