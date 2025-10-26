#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>
#include <pybind11/stl_optional.h>

#include "fin/api/ScenarioService.hpp"
#include "fin/io/Pipeline.hpp"

namespace py = pybind11;

namespace
{
    py::dict scenario_result_to_dict(const fin::app::ScenarioResult &result)
    {
        py::dict out;
        out["candles"] = result.candles;
        out["warmup_candles"] = result.warmup_candles;
        out["feature_rows"] = result.feature_rows;
        out["validation_samples"] = result.validation_samples;
        out["validation_rmse"] = result.validation_samples > 0 ? result.validation_rmse : py::float_(0.0);
        out["model_saved"] = result.model_saved;

        py::dict training;
        training["samples"] = result.training.samples;
        training["mse"] = result.training.mse;
        training["bias"] = result.training.model.bias();

        py::dict weights;
        for (const auto &[name, weight] : result.training.model.named_weights())
            weights[py::str(name)] = weight;
        training["weights"] = weights;
        out["training"] = training;

        py::dict metrics;
        metrics["final_cash"] = result.metrics.final_cash;
        metrics["pnl"] = result.metrics.pnl;
        metrics["return_pct"] = result.metrics.return_pct;
        metrics["trades"] = result.metrics.trades;
        metrics["wins"] = result.metrics.wins;
        metrics["losses"] = result.metrics.losses;
        metrics["max_drawdown"] = result.metrics.max_drawdown;
        out["metrics"] = metrics;

        py::list preview;
        for (const auto &row : result.validation_preview)
        {
            py::dict entry;
            entry["ts_ms"] = row.ts_ms;
            entry["predicted_delta"] = row.predicted_delta;
            entry["actual_delta"] = row.actual_delta;
            preview.append(std::move(entry));
        }
        out["validation_preview"] = preview;
        return out;
    }
}

PYBIND11_MODULE(aiquant_api, m)
{
    m.doc() = "Python bindings for the AiQuant Scenario service";

    py::enum_<fin::io::Timeframe>(m, "Timeframe")
        .value("S1", fin::io::Timeframe::S1)
        .value("S5", fin::io::Timeframe::S5)
        .value("M1", fin::io::Timeframe::M1)
        .value("M5", fin::io::Timeframe::M5)
        .value("H1", fin::io::Timeframe::H1);

    py::class_<fin::app::ScenarioConfig>(m, "ScenarioConfig")
        .def(py::init<>())
        .def_readwrite("ticks_path", &fin::app::ScenarioConfig::ticks_path)
        .def_readwrite("timeframe", &fin::app::ScenarioConfig::timeframe)
        .def_readwrite("train_ratio", &fin::app::ScenarioConfig::train_ratio)
        .def_readwrite("ridge_lambda", &fin::app::ScenarioConfig::ridge_lambda)
        .def_readwrite("ema_fast", &fin::app::ScenarioConfig::ema_fast)
        .def_readwrite("ema_slow", &fin::app::ScenarioConfig::ema_slow)
        .def_readwrite("rsi_period", &fin::app::ScenarioConfig::rsi_period)
        .def_readwrite("macd_fast", &fin::app::ScenarioConfig::macd_fast)
        .def_readwrite("macd_slow", &fin::app::ScenarioConfig::macd_slow)
        .def_readwrite("macd_signal", &fin::app::ScenarioConfig::macd_signal)
        .def_readwrite("rsi_buy", &fin::app::ScenarioConfig::rsi_buy)
        .def_readwrite("rsi_sell", &fin::app::ScenarioConfig::rsi_sell)
        .def_readwrite("use_ema_crossover", &fin::app::ScenarioConfig::use_ema_crossover)
        .def_readwrite("initial_cash", &fin::app::ScenarioConfig::initial_cash)
        .def_readwrite("trade_qty", &fin::app::ScenarioConfig::trade_qty)
        .def_readwrite("fee_per_trade", &fin::app::ScenarioConfig::fee_per_trade)
        .def_readwrite("model_output_path", &fin::app::ScenarioConfig::model_output_path)
        .def_readwrite("validation_preview_limit", &fin::app::ScenarioConfig::validation_preview_limit);

    py::class_<fin::api::ScenarioService>(m, "ScenarioService")
        .def(py::init<>())
        .def("load_file", &fin::api::ScenarioService::load_file)
        .def("run_config", [](const fin::api::ScenarioService &svc, const fin::app::ScenarioConfig &cfg) {
            return scenario_result_to_dict(svc.run(cfg));
        })
        .def("run_file", [](const fin::api::ScenarioService &svc, const std::string &path) {
            return scenario_result_to_dict(svc.run_file(path));
        });
}
