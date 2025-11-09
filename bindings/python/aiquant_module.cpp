#include <optional>
#include <string>
#include <unordered_map>

#include <pybind11/pybind11.h>

#include "fin/api/ScenarioService.hpp"

namespace py = pybind11;

namespace
{
    fin::api::ScenarioService &service()
    {
        static fin::api::ScenarioService svc;
        return svc;
    }

    const char *timeframe_to_str(fin::io::Timeframe tf)
    {
        switch (tf)
        {
        case fin::io::Timeframe::S1: return "S1";
        case fin::io::Timeframe::S5: return "S5";
        case fin::io::Timeframe::M5: return "M5";
        case fin::io::Timeframe::H1: return "H1";
        case fin::io::Timeframe::M1:
        default:
            return "M1";
        }
    }

    std::optional<fin::io::Timeframe> parse_timeframe(const std::string &value)
    {
        static const std::unordered_map<std::string, fin::io::Timeframe> map{{"S1", fin::io::Timeframe::S1},
                                                                             {"S5", fin::io::Timeframe::S5},
                                                                             {"M1", fin::io::Timeframe::M1},
                                                                             {"M5", fin::io::Timeframe::M5},
                                                                             {"H1", fin::io::Timeframe::H1}};
        auto it = map.find(value);
        return it == map.end() ? std::nullopt : std::optional<fin::io::Timeframe>(it->second);
    }

    py::object get_if_present(const py::dict &dict, const char *name, bool *present = nullptr)
    {
        py::str key(name);
        if (!dict.contains(key))
        {
            if (present)
                *present = false;
            return py::none();
        }
        if (present)
            *present = true;
        return dict[key];
    }

    bool set_double(const py::dict &dict, const char *name, double &target, std::string &error)
    {
        bool present = false;
        py::object value = get_if_present(dict, name, &present);
        if (!present)
            return true;
        try
        {
            target = value.cast<double>();
            return true;
        }
        catch (const py::cast_error &)
        {
            error = std::string("Invalid numeric value for ") + name;
            return false;
        }
    }

    bool set_size(const py::dict &dict, const char *name, std::size_t &target, std::string &error)
    {
        bool present = false;
        py::object value = get_if_present(dict, name, &present);
        if (!present)
            return true;
        try
        {
            long long v = value.cast<long long>();
            if (v < 0)
            {
                error = std::string("Invalid integer value for ") + name;
                return false;
            }
            target = static_cast<std::size_t>(v);
            return true;
        }
        catch (const py::cast_error &)
        {
            error = std::string("Invalid integer value for ") + name;
            return false;
        }
    }

    bool set_bool(const py::dict &dict, const char *name, bool &target, std::string &error)
    {
        bool present = false;
        py::object value = get_if_present(dict, name, &present);
        if (!present)
            return true;
        try
        {
            target = value.cast<bool>();
            return true;
        }
        catch (const py::cast_error &)
        {
            error = std::string("Invalid boolean for ") + name;
            return false;
        }
    }

    bool set_optional_double(const py::dict &dict, const char *name, std::optional<double> &target, std::string &error)
    {
        bool present = false;
        py::object value = get_if_present(dict, name, &present);
        if (!present)
            return true;
        if (value.is_none())
        {
            target.reset();
            return true;
        }
        try
        {
            target = value.cast<double>();
            return true;
        }
        catch (const py::cast_error &)
        {
            error = std::string("Invalid numeric value for ") + name;
            return false;
        }
    }

    bool mapping_to_config(const py::dict &dict, fin::app::ScenarioConfig &cfg, std::string &error)
    {
        bool present = false;
        if (py::object ticks = get_if_present(dict, "ticks_path", &present); present)
        {
            if (!py::isinstance<py::str>(ticks))
            {
                error = "ticks_path must be a string";
                return false;
            }
            cfg.ticks_path = ticks.cast<std::string>();
        }

        if (py::object tf = get_if_present(dict, "timeframe", &present); present)
        {
            if (!py::isinstance<py::str>(tf))
            {
                error = "timeframe must be string";
                return false;
            }
            std::string token = tf.cast<std::string>();
            if (auto parsed = parse_timeframe(token))
                cfg.timeframe = *parsed;
            else
            {
                error = "Unknown timeframe: " + token;
                return false;
            }
        }

        if (!set_double(dict, "train_ratio", cfg.train_ratio, error)) return false;
        if (!set_double(dict, "ridge_lambda", cfg.ridge_lambda, error)) return false;
        if (!set_size(dict, "ema_fast", cfg.ema_fast, error)) return false;
        if (!set_size(dict, "ema_slow", cfg.ema_slow, error)) return false;
        if (!set_size(dict, "rsi_period", cfg.rsi_period, error)) return false;
        if (!set_size(dict, "macd_fast", cfg.macd_fast, error)) return false;
        if (!set_size(dict, "macd_slow", cfg.macd_slow, error)) return false;
        if (!set_size(dict, "macd_signal", cfg.macd_signal, error)) return false;
        if (!set_double(dict, "rsi_buy", cfg.rsi_buy, error)) return false;
        if (!set_double(dict, "rsi_sell", cfg.rsi_sell, error)) return false;
        if (!set_bool(dict, "use_ema_crossover", cfg.use_ema_crossover, error)) return false;
        if (!set_optional_double(dict, "initial_cash", cfg.initial_cash, error)) return false;
        if (!set_optional_double(dict, "trade_qty", cfg.trade_qty, error)) return false;
        if (!set_optional_double(dict, "fee_per_trade", cfg.fee_per_trade, error)) return false;
        if (!set_size(dict, "validation_preview_limit", cfg.validation_preview_limit, error)) return false;

        if (py::object model = get_if_present(dict, "model_output_path", &present); present)
        {
            if (model.is_none())
            {
                cfg.model_output_path.reset();
            }
            else if (py::isinstance<py::str>(model))
            {
                cfg.model_output_path = model.cast<std::string>();
            }
            else
            {
                error = "model_output_path must be string";
                return false;
            }
        }

        return true;
    }

    py::dict scenario_result_to_dict(const fin::app::ScenarioConfig &cfg, const fin::app::ScenarioResult &result)
    {
        py::dict root;
        root["ticks_path"] = cfg.ticks_path;
        root["timeframe"] = timeframe_to_str(cfg.timeframe);
        root["candles"] = result.candles;
        root["warmup_candles"] = result.warmup_candles;
        root["feature_rows"] = result.feature_rows;
        root["training_samples"] = result.training.samples;
        root["validation_samples"] = result.validation_samples;
        root["training_mse"] = result.training.mse;
        root["validation_rmse"] = result.validation_rmse;
        root["model_saved"] = result.model_saved;

        py::dict metrics;
        metrics["final_cash"] = result.metrics.final_cash;
        metrics["pnl"] = result.metrics.pnl;
        metrics["return_pct"] = result.metrics.return_pct;
        metrics["trades"] = result.metrics.trades;
        metrics["wins"] = result.metrics.wins;
        metrics["losses"] = result.metrics.losses;
        metrics["max_drawdown"] = result.metrics.max_drawdown;
        root["metrics"] = std::move(metrics);

        py::list preview;
        for (const auto &row : result.validation_preview)
        {
            py::dict entry;
            entry["ts_ms"] = row.ts_ms;
            entry["predicted_delta"] = row.predicted_delta;
            entry["actual_delta"] = row.actual_delta;
            preview.append(std::move(entry));
        }
        root["validation_preview"] = std::move(preview);
        return root;
    }

    py::dict config_to_dict(const fin::app::ScenarioConfig &cfg)
    {
        py::dict dict;
        dict["ticks_path"] = cfg.ticks_path;
        dict["timeframe"] = timeframe_to_str(cfg.timeframe);
        dict["train_ratio"] = cfg.train_ratio;
        dict["ridge_lambda"] = cfg.ridge_lambda;
        dict["ema_fast"] = cfg.ema_fast;
        dict["ema_slow"] = cfg.ema_slow;
        dict["rsi_period"] = cfg.rsi_period;
        dict["macd_fast"] = cfg.macd_fast;
        dict["macd_slow"] = cfg.macd_slow;
        dict["macd_signal"] = cfg.macd_signal;
        dict["rsi_buy"] = cfg.rsi_buy;
        dict["rsi_sell"] = cfg.rsi_sell;
        dict["use_ema_crossover"] = cfg.use_ema_crossover;
        if (cfg.initial_cash)
            dict["initial_cash"] = *cfg.initial_cash;
        if (cfg.trade_qty)
            dict["trade_qty"] = *cfg.trade_qty;
        if (cfg.fee_per_trade)
            dict["fee_per_trade"] = *cfg.fee_per_trade;
        dict["validation_preview_limit"] = cfg.validation_preview_limit;
        if (cfg.model_output_path)
            dict["model_output_path"] = *cfg.model_output_path;
        return dict;
    }

    py::dict run_file(const std::string &path)
    {
        auto cfg = service().load_file(path);
        auto result = service().run(cfg);
        return scenario_result_to_dict(cfg, result);
    }

    py::dict load_file(const std::string &path)
    {
        auto cfg = service().load_file(path);
        return config_to_dict(cfg);
    }

    py::dict run_config(py::object cfg_obj)
    {
        if (!py::isinstance<py::dict>(cfg_obj))
            throw py::value_error("config must be dict");

        auto cfg_dict = cfg_obj.cast<py::dict>();
        fin::app::ScenarioConfig cfg{};
        std::string error;
        if (!mapping_to_config(cfg_dict, cfg, error))
            throw py::value_error(error);
        if (cfg.ticks_path.empty())
            throw py::value_error("config dict missing ticks_path");

        auto result = service().run(cfg);
        return scenario_result_to_dict(cfg, result);
    }
}

PYBIND11_MODULE(aiquant_api, m)
{
    m.doc() = "AiQuant Python bindings";
    m.def("run_file", &run_file, py::arg("path"), "Run a scenario defined in a file");
    m.def("load_file", &load_file, py::arg("path"), "Load scenario.ini into a dict");
    m.def("run_config", &run_config, py::arg("config"), "Run from dictionary config");
}
