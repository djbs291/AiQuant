#include "fin/app/ScenarioConfigIO.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "fin/app/ScenarioUtils.hpp"
#include "fin/indicators/FeatureSpec.hpp" // find_feature: reject unknown feature names

namespace fin::app
{
    namespace
    {
        void trim_inplace(std::string &s)
        {
            auto not_space = [](unsigned char ch)
            { return !std::isspace(ch); };
            auto begin = std::find_if(s.begin(), s.end(), not_space);
            if (begin == s.end())
            {
                s.clear();
                return;
            }

            auto end = std::find_if(s.rbegin(), s.rend(), not_space).base();
            s.assign(begin, end);
        }

        std::optional<bool> parse_bool_value(const std::string &value)
        {
            std::string token;
            token.reserve(value.size());
            for (char ch : value)
            {
                if (!std::isspace(static_cast<unsigned char>(ch)))
                    token.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            }

            if (token == "true" || token == "1" || token == "yes" || token == "on")
                return true;
            if (token == "false" || token == "0" || token == "no" || token == "off")
                return false;
            return std::nullopt;
        }

        // from_chars accepts "nan" and "inf". Refusing them here names the line, which the
        // range checks at the end of the load cannot, and every one of those checks is a
        // comparison that NaN would pass by being false both ways.
        bool parse_double_value(const std::string &text, double &out)
        {
            const char *begin = text.c_str();
            const char *end = begin + text.size();
            double v = 0.0;
            auto [ptr, ec] = std::from_chars(begin, end, v);
            if (ec != std::errc{} || ptr != end || !std::isfinite(v))
                return false;
            out = v;
            return true;
        }

        bool parse_size_value(const std::string &text, std::size_t &out)
        {
            const char *begin = text.c_str();
            const char *end = begin + text.size();
            auto [ptr, ec] = std::from_chars(begin, end, out);
            return ec == std::errc{} && ptr == end;
        }

        // Comma-separated list, lowercased and trimmed. Empty items are rejected rather than
        // skipped, so `a,,b` is a typo the user hears about.
        bool parse_list_value(const std::string &text, std::vector<std::string> &out, std::string &error)
        {
            std::vector<std::string> items;
            std::istringstream stream(text);
            std::string item;
            while (std::getline(stream, item, ','))
            {
                trim_inplace(item);
                if (item.empty())
                {
                    error = "empty item in list";
                    return false;
                }
                std::transform(item.begin(), item.end(), item.begin(), [](unsigned char ch)
                               { return static_cast<char>(std::tolower(ch)); });
                items.push_back(item);
            }

            if (items.empty())
            {
                error = "list is empty";
                return false;
            }

            // A repeated feature makes two identical columns, which is exactly singular. The
            // ridge term hides it just enough that the weight gets split arbitrarily between
            // the twins instead of failing, so reject it here.
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                for (std::size_t j = i + 1; j < items.size(); ++j)
                {
                    if (items[i] == items[j])
                    {
                        error = "duplicate item '" + items[i] + "'";
                        return false;
                    }
                }
            }

            out = std::move(items);
            return true;
        }

        // Every alias maps to one canonical key, so the dispatch below has a single name per
        // field and a repeated field is caught whichever spelling each occurrence uses. A key
        // with no entry is its own canonical name.
        std::string canonical_key(const std::string &lowered)
        {
            static const std::pair<std::string_view, std::string_view> aliases[] = {
                {"ticks_path", "ticks"},
                {"data", "ticks"},
                {"timeframe", "tf"},
                {"ridge_lambda", "ridge"},
                {"sma_period", "sma"},
                {"atr_period", "atr"},
                {"adx_period", "adx"},
                {"stoch_k_period", "stoch_k"},
                {"stoch_d_period", "stoch_d"},
                {"zscore_period", "zscore"},
                {"momentum_period", "momentum"},
                {"initial_cash", "cash"},
                {"trade_qty", "qty"},
                {"fee_per_trade", "fee"},
                {"model_output", "model_out"},
                {"preview_limit", "preview"},
                {"sgd_lr", "sgd_learning_rate"},
                {"online", "online_update"},
            };
            for (const auto &[alias, canonical] : aliases)
            {
                if (lowered == alias)
                    return std::string(canonical);
            }
            return lowered;
        }

        // A '#' starts an inline comment only after whitespace, the convention Python's
        // configparser uses for inline_comment_prefixes. Anywhere else it belongs to the
        // value: `ticks = runs#3.csv` used to load as `runs`, and `rsi = 10#x` as 10.
        std::size_t find_inline_comment(const std::string &line)
        {
            for (std::size_t i = 1; i < line.size(); ++i)
            {
                if (line[i] == '#' && std::isspace(static_cast<unsigned char>(line[i - 1])))
                    return i;
            }
            return std::string::npos;
        }
    } // namespace

    bool load_scenario_file(const std::string &path, ScenarioConfig &cfg, std::string &error)
    {
        std::ifstream in(path);
        if (!in)
        {
            error = "Failed to open scenario file: " + path;
            return false;
        }

        // Which line set each field, by canonical name. A repeat used to override silently, so
        // `rsi = 10` further down a file than `rsi = 20` won without a word, and so did
        // `sma_period` over `sma`.
        std::unordered_map<std::string, std::pair<std::string, std::size_t>> seen;

        std::string line;
        std::size_t line_no = 0;
        while (std::getline(in, line))
        {
            ++line_no;
            trim_inplace(line);
            if (line.empty() || line[0] == '#')
                continue;

            auto comment_pos = find_inline_comment(line);
            if (comment_pos != std::string::npos)
            {
                line.erase(comment_pos);
                trim_inplace(line);
                if (line.empty())
                    continue;
            }

            auto eq = line.find('=');
            if (eq == std::string::npos)
            {
                error = "Invalid line " + std::to_string(line_no) + " (expected key=value)";
                return false;
            }

            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            trim_inplace(key);
            trim_inplace(value);
            if (key.empty())
            {
                error = "Missing key at line " + std::to_string(line_no);
                return false;
            }

            std::string lowered(key.size(), '\0');
            std::transform(key.begin(), key.end(), lowered.begin(), [](unsigned char ch)
                           { return static_cast<char>(std::tolower(ch)); });

            const std::string name = canonical_key(lowered);
            // use_ema_crossover and no_ema_xover are one field spelled with opposite meanings,
            // so setting both is a repeat even though they dispatch separately.
            const std::string field = (name == "no_ema_xover") ? "use_ema_crossover" : name;
            if (auto [it, inserted] = seen.try_emplace(field, key, line_no); !inserted)
            {
                error = "Duplicate key '" + key + "' at line " + std::to_string(line_no) +
                        ": already set by '" + it->second.first + "' at line " +
                        std::to_string(it->second.second);
                return false;
            }

            if (name == "ticks")
            {
                cfg.ticks_path = value;
            }
            else if (name == "features")
            {
                std::string list_error;
                if (!parse_list_value(value, cfg.features, list_error))
                {
                    error = "Invalid features at line " + std::to_string(line_no) + ": " + list_error;
                    return false;
                }
                // An unknown feature is refused like an unknown key: dropping it would quietly
                // train a different model than the one asked for.
                for (const auto &name : cfg.features)
                {
                    if (fin::indicators::find_feature(name) == nullptr)
                    {
                        error = "Unknown feature '" + name + "' at line " + std::to_string(line_no);
                        return false;
                    }
                }
            }
            else if (name == "sma")
            {
                if (!parse_size_value(value, cfg.sma_period))
                {
                    error = "Invalid sma period at line " + std::to_string(line_no);
                    return false;
                }
            }
            else if (name == "bb_period")
            {
                if (!parse_size_value(value, cfg.bb_period))
                {
                    error = "Invalid bb_period at line " + std::to_string(line_no);
                    return false;
                }
            }
            else if (name == "bb_k")
            {
                if (!parse_double_value(value, cfg.bb_k))
                {
                    error = "Invalid bb_k at line " + std::to_string(line_no);
                    return false;
                }
            }
            else if (name == "atr")
            {
                if (!parse_size_value(value, cfg.atr_period))
                {
                    error = "Invalid atr period at line " + std::to_string(line_no);
                    return false;
                }
            }
            else if (name == "adx")
            {
                if (!parse_size_value(value, cfg.adx_period))
                {
                    error = "Invalid adx period at line " + std::to_string(line_no);
                    return false;
                }
            }
            else if (name == "stoch_k")
            {
                if (!parse_size_value(value, cfg.stoch_k_period))
                {
                    error = "Invalid stoch_k period at line " + std::to_string(line_no);
                    return false;
                }
            }
            else if (name == "stoch_d")
            {
                if (!parse_size_value(value, cfg.stoch_d_period))
                {
                    error = "Invalid stoch_d period at line " + std::to_string(line_no);
                    return false;
                }
            }
            else if (name == "zscore")
            {
                if (!parse_size_value(value, cfg.zscore_period))
                {
                    error = "Invalid zscore period at line " + std::to_string(line_no);
                    return false;
                }
            }
            else if (name == "momentum")
            {
                if (!parse_size_value(value, cfg.momentum_period))
                {
                    error = "Invalid momentum period at line " + std::to_string(line_no);
                    return false;
                }
            }
            else if (name == "tf")
            {
                if (auto tf = parse_timeframe_token(value))
                    cfg.timeframe = *tf;
                else
                {
                    error = "Unknown timeframe '" + value + "' at line " + std::to_string(line_no);
                    return false;
                }
            }
            else if (name == "train_ratio")
            {
                double v = 0.0;
                if (!parse_double_value(value, v))
                {
                    error = "Invalid train_ratio at line " + std::to_string(line_no);
                    return false;
                }
                cfg.train_ratio = v;
            }
            else if (name == "ridge")
            {
                double v = 0.0;
                if (!parse_double_value(value, v))
                {
                    error = "Invalid ridge value at line " + std::to_string(line_no);
                    return false;
                }
                cfg.ridge_lambda = v;
            }
            else if (name == "ema_fast")
            {
                std::size_t v = 0;
                if (!parse_size_value(value, v))
                {
                    error = "Invalid ema_fast at line " + std::to_string(line_no);
                    return false;
                }
                cfg.ema_fast = v;
            }
            else if (name == "ema_slow")
            {
                std::size_t v = 0;
                if (!parse_size_value(value, v))
                {
                    error = "Invalid ema_slow at line " + std::to_string(line_no);
                    return false;
                }
                cfg.ema_slow = v;
            }
            else if (name == "rsi")
            {
                std::size_t v = 0;
                if (!parse_size_value(value, v))
                {
                    error = "Invalid rsi period at line " + std::to_string(line_no);
                    return false;
                }
                cfg.rsi_period = v;
            }
            else if (name == "macd_fast")
            {
                std::size_t v = 0;
                if (!parse_size_value(value, v))
                {
                    error = "Invalid macd_fast at line " + std::to_string(line_no);
                    return false;
                }
                cfg.macd_fast = v;
            }
            else if (name == "macd_slow")
            {
                std::size_t v = 0;
                if (!parse_size_value(value, v))
                {
                    error = "Invalid macd_slow at line " + std::to_string(line_no);
                    return false;
                }
                cfg.macd_slow = v;
            }
            else if (name == "macd_signal")
            {
                std::size_t v = 0;
                if (!parse_size_value(value, v))
                {
                    error = "Invalid macd_signal at line " + std::to_string(line_no);
                    return false;
                }
                cfg.macd_signal = v;
            }
            else if (name == "rsi_buy")
            {
                double v = 0.0;
                if (!parse_double_value(value, v))
                {
                    error = "Invalid rsi_buy at line " + std::to_string(line_no);
                    return false;
                }
                cfg.rsi_buy = v;
            }
            else if (name == "rsi_sell")
            {
                double v = 0.0;
                if (!parse_double_value(value, v))
                {
                    error = "Invalid rsi_sell at line " + std::to_string(line_no);
                    return false;
                }
                cfg.rsi_sell = v;
            }
            else if (name == "use_ema_crossover")
            {
                auto b = parse_bool_value(value);
                if (!b)
                {
                    error = "Invalid boolean for use_ema_crossover at line " + std::to_string(line_no);
                    return false;
                }
                cfg.use_ema_crossover = *b;
            }
            else if (name == "no_ema_xover")
            {
                auto b = parse_bool_value(value);
                if (!b)
                {
                    error = "Invalid boolean for no_ema_xover at line " + std::to_string(line_no);
                    return false;
                }
                cfg.use_ema_crossover = !*b;
            }
            else if (name == "cash")
            {
                double v = 0.0;
                if (!parse_double_value(value, v))
                {
                    error = "Invalid cash value at line " + std::to_string(line_no);
                    return false;
                }
                cfg.initial_cash = v;
            }
            else if (name == "qty")
            {
                double v = 0.0;
                if (!parse_double_value(value, v))
                {
                    error = "Invalid qty value at line " + std::to_string(line_no);
                    return false;
                }
                cfg.trade_qty = v;
            }
            else if (name == "fee")
            {
                double v = 0.0;
                if (!parse_double_value(value, v))
                {
                    error = "Invalid fee value at line " + std::to_string(line_no);
                    return false;
                }
                cfg.fee_per_trade = v;
            }
            else if (name == "model_out")
            {
                cfg.model_output_path = value;
            }
            else if (name == "preview")
            {
                std::size_t v = 0;
                if (!parse_size_value(value, v))
                {
                    error = "Invalid preview limit at line " + std::to_string(line_no);
                    return false;
                }
                cfg.validation_preview_limit = v;
            }
            else if (name == "model")
            {
                std::string token = value;
                std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch)
                               { return static_cast<char>(std::tolower(ch)); });
                if (token == "ridge" || token == "linear")
                    cfg.model = ModelKind::Ridge;
                else if (token == "sgd")
                    cfg.model = ModelKind::Sgd;
                else
                {
                    error = "Unknown model '" + value + "' at line " + std::to_string(line_no) +
                            " (expected ridge or sgd)";
                    return false;
                }
            }
            else if (name == "sgd_learning_rate")
            {
                double v = 0.0;
                if (!parse_double_value(value, v))
                {
                    error = "Invalid sgd_learning_rate at line " + std::to_string(line_no);
                    return false;
                }
                cfg.sgd.learning_rate = v;
            }
            else if (name == "sgd_l2")
            {
                double v = 0.0;
                if (!parse_double_value(value, v))
                {
                    error = "Invalid sgd_l2 at line " + std::to_string(line_no);
                    return false;
                }
                cfg.sgd.l2 = v;
            }
            else if (name == "sgd_epochs")
            {
                std::size_t v = 0;
                if (!parse_size_value(value, v))
                {
                    error = "Invalid sgd_epochs at line " + std::to_string(line_no);
                    return false;
                }
                cfg.sgd.epochs = v;
            }
            else if (name == "sgd_power_t")
            {
                double v = 0.0;
                if (!parse_double_value(value, v))
                {
                    error = "Invalid sgd_power_t at line " + std::to_string(line_no);
                    return false;
                }
                cfg.sgd.power_t = v;
            }
            else if (name == "sgd_standardize")
            {
                auto b = parse_bool_value(value);
                if (!b)
                {
                    error = "Invalid boolean for sgd_standardize at line " + std::to_string(line_no);
                    return false;
                }
                cfg.sgd.standardize = *b;
            }
            else if (name == "online_update")
            {
                auto b = parse_bool_value(value);
                if (!b)
                {
                    error = "Invalid boolean for online_update at line " + std::to_string(line_no);
                    return false;
                }
                cfg.online_update = *b;
            }
            else if (name == "symbol")
            {
                // Case-sensitive, unlike the keys: ticker symbols are not ours to fold.
                cfg.symbol = value;
            }
            else
            {
                // Silence used to be the policy here, which meant a typo trained a different
                // model than the one asked for and said nothing: `rsi_peroid = 20` left the
                // period at its default of 14 and the run looked entirely normal.
                error = "Unknown key '" + key + "' at line " + std::to_string(line_no);
                return false;
            }
        }

        if (cfg.ticks_path.empty())
        {
            error = "Scenario file missing 'ticks' path";
            return false;
        }

        // Range checks in one pass rather than at each parse site, shared with run_scenario so
        // a config built from CLI flags or a Python dict meets the same rules as this file.
        return validate_scenario_config(cfg, error);
    }
}
