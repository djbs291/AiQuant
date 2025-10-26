#include "fin/app/ScenarioConfigIO.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <optional>

#include "fin/app/ScenarioUtils.hpp"

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

        bool parse_double_value(const std::string &text, double &out)
        {
            const char *begin = text.c_str();
            const char *end = begin + text.size();
            auto [ptr, ec] = std::from_chars(begin, end, out);
            return ec == std::errc{} && ptr == end;
        }

        bool parse_size_value(const std::string &text, std::size_t &out)
        {
            const char *begin = text.c_str();
            const char *end = begin + text.size();
            auto [ptr, ec] = std::from_chars(begin, end, out);
            return ec == std::errc{} && ptr == end;
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

        std::string line;
        std::size_t line_no = 0;
        while (std::getline(in, line))
        {
            ++line_no;
            trim_inplace(line);
            if (line.empty() || line[0] == '#')
                continue;

            auto comment_pos = line.find('#');
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

            if (lowered == "ticks" || lowered == "ticks_path" || lowered == "data")
            {
                cfg.ticks_path = value;
            }
            else if (lowered == "tf" || lowered == "timeframe")
            {
                if (auto tf = parse_timeframe_token(value))
                    cfg.timeframe = *tf;
                else
                {
                    error = "Unknown timeframe '" + value + "' at line " + std::to_string(line_no);
                    return false;
                }
            }
            else if (lowered == "train_ratio")
            {
                double v = 0.0;
                if (!parse_double_value(value, v))
                {
                    error = "Invalid train_ratio at line " + std::to_string(line_no);
                    return false;
                }
                cfg.train_ratio = v;
            }
            else if (lowered == "ridge" || lowered == "ridge_lambda")
            {
                double v = 0.0;
                if (!parse_double_value(value, v))
                {
                    error = "Invalid ridge value at line " + std::to_string(line_no);
                    return false;
                }
                cfg.ridge_lambda = v;
            }
            else if (lowered == "ema_fast")
            {
                std::size_t v = 0;
                if (!parse_size_value(value, v))
                {
                    error = "Invalid ema_fast at line " + std::to_string(line_no);
                    return false;
                }
                cfg.ema_fast = v;
            }
            else if (lowered == "ema_slow")
            {
                std::size_t v = 0;
                if (!parse_size_value(value, v))
                {
                    error = "Invalid ema_slow at line " + std::to_string(line_no);
                    return false;
                }
                cfg.ema_slow = v;
            }
            else if (lowered == "rsi")
            {
                std::size_t v = 0;
                if (!parse_size_value(value, v))
                {
                    error = "Invalid rsi period at line " + std::to_string(line_no);
                    return false;
                }
                cfg.rsi_period = v;
            }
            else if (lowered == "macd_fast")
            {
                std::size_t v = 0;
                if (!parse_size_value(value, v))
                {
                    error = "Invalid macd_fast at line " + std::to_string(line_no);
                    return false;
                }
                cfg.macd_fast = v;
            }
            else if (lowered == "macd_slow")
            {
                std::size_t v = 0;
                if (!parse_size_value(value, v))
                {
                    error = "Invalid macd_slow at line " + std::to_string(line_no);
                    return false;
                }
                cfg.macd_slow = v;
            }
            else if (lowered == "macd_signal")
            {
                std::size_t v = 0;
                if (!parse_size_value(value, v))
                {
                    error = "Invalid macd_signal at line " + std::to_string(line_no);
                    return false;
                }
                cfg.macd_signal = v;
            }
            else if (lowered == "rsi_buy")
            {
                double v = 0.0;
                if (!parse_double_value(value, v))
                {
                    error = "Invalid rsi_buy at line " + std::to_string(line_no);
                    return false;
                }
                cfg.rsi_buy = v;
            }
            else if (lowered == "rsi_sell")
            {
                double v = 0.0;
                if (!parse_double_value(value, v))
                {
                    error = "Invalid rsi_sell at line " + std::to_string(line_no);
                    return false;
                }
                cfg.rsi_sell = v;
            }
            else if (lowered == "use_ema_crossover")
            {
                auto b = parse_bool_value(value);
                if (!b)
                {
                    error = "Invalid boolean for use_ema_crossover at line " + std::to_string(line_no);
                    return false;
                }
                cfg.use_ema_crossover = *b;
            }
            else if (lowered == "no_ema_xover")
            {
                auto b = parse_bool_value(value);
                if (!b)
                {
                    error = "Invalid boolean for no_ema_xover at line " + std::to_string(line_no);
                    return false;
                }
                cfg.use_ema_crossover = !*b;
            }
            else if (lowered == "cash" || lowered == "initial_cash")
            {
                double v = 0.0;
                if (!parse_double_value(value, v))
                {
                    error = "Invalid cash value at line " + std::to_string(line_no);
                    return false;
                }
                cfg.initial_cash = v;
            }
            else if (lowered == "qty" || lowered == "trade_qty")
            {
                double v = 0.0;
                if (!parse_double_value(value, v))
                {
                    error = "Invalid qty value at line " + std::to_string(line_no);
                    return false;
                }
                cfg.trade_qty = v;
            }
            else if (lowered == "fee" || lowered == "fee_per_trade")
            {
                double v = 0.0;
                if (!parse_double_value(value, v))
                {
                    error = "Invalid fee value at line " + std::to_string(line_no);
                    return false;
                }
                cfg.fee_per_trade = v;
            }
            else if (lowered == "model_out" || lowered == "model_output")
            {
                cfg.model_output_path = value;
            }
            else if (lowered == "preview" || lowered == "preview_limit")
            {
                std::size_t v = 0;
                if (!parse_size_value(value, v))
                {
                    error = "Invalid preview limit at line " + std::to_string(line_no);
                    return false;
                }
                cfg.validation_preview_limit = v;
            }
            else
            {
                // Unknown keys ignored for MVP.
            }
        }

        if (cfg.ticks_path.empty())
        {
            error = "Scenario file missing 'ticks' path";
            return false;
        }

        return true;
    }
}

