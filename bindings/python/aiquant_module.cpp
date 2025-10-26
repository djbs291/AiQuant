#include <Python.h>

#include <optional>
#include <string>
#include <unordered_map>

#include "fin/api/ScenarioService.hpp"

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

    void dict_set(PyObject *dict, const char *key, PyObject *value)
    {
        PyDict_SetItemString(dict, key, value);
        Py_DECREF(value);
    }

    PyObject *scenario_result_to_dict(const fin::app::ScenarioConfig &cfg, const fin::app::ScenarioResult &result)
    {
        PyObject *root = PyDict_New();
        dict_set(root, "ticks_path", PyUnicode_FromString(cfg.ticks_path.c_str()));
        dict_set(root, "timeframe", PyUnicode_FromString(timeframe_to_str(cfg.timeframe)));
        dict_set(root, "candles", PyLong_FromUnsignedLongLong(result.candles));
        dict_set(root, "warmup_candles", PyLong_FromUnsignedLongLong(result.warmup_candles));
        dict_set(root, "feature_rows", PyLong_FromUnsignedLongLong(result.feature_rows));
        dict_set(root, "training_samples", PyLong_FromUnsignedLongLong(result.training.samples));
        dict_set(root, "validation_samples", PyLong_FromUnsignedLongLong(result.validation_samples));
        dict_set(root, "training_mse", PyFloat_FromDouble(result.training.mse));
        dict_set(root, "validation_rmse", PyFloat_FromDouble(result.validation_rmse));
        Py_INCREF(result.model_saved ? Py_True : Py_False);
        PyDict_SetItemString(root, "model_saved", result.model_saved ? Py_True : Py_False);

        PyObject *metrics = PyDict_New();
        dict_set(metrics, "final_cash", PyFloat_FromDouble(result.metrics.final_cash));
        dict_set(metrics, "pnl", PyFloat_FromDouble(result.metrics.pnl));
        dict_set(metrics, "return_pct", PyFloat_FromDouble(result.metrics.return_pct));
        dict_set(metrics, "trades", PyLong_FromUnsignedLongLong(result.metrics.trades));
        dict_set(metrics, "wins", PyLong_FromUnsignedLongLong(result.metrics.wins));
        dict_set(metrics, "losses", PyLong_FromUnsignedLongLong(result.metrics.losses));
        dict_set(metrics, "max_drawdown", PyFloat_FromDouble(result.metrics.max_drawdown));
        PyDict_SetItemString(root, "metrics", metrics);
        Py_DECREF(metrics);

        PyObject *preview = PyList_New(result.validation_preview.size());
        for (std::size_t i = 0; i < result.validation_preview.size(); ++i)
        {
            const auto &row = result.validation_preview[i];
            PyObject *entry = PyDict_New();
            dict_set(entry, "ts_ms", PyLong_FromLongLong(row.ts_ms));
            dict_set(entry, "predicted_delta", PyFloat_FromDouble(row.predicted_delta));
            dict_set(entry, "actual_delta", PyFloat_FromDouble(row.actual_delta));
            PyList_SET_ITEM(preview, i, entry);
        }
        PyDict_SetItemString(root, "validation_preview", preview);
        Py_DECREF(preview);
        return root;
    }

    bool set_double(PyObject *dict, const char *name, double &target, std::string &error)
    {
        PyObject *value = PyDict_GetItemString(dict, name);
        if (!value)
            return true;
        double v = PyFloat_AsDouble(value);
        if (PyErr_Occurred())
        {
            error = std::string("Invalid numeric value for ") + name;
            PyErr_Clear();
            return false;
        }
        target = v;
        return true;
    }

    bool set_size(PyObject *dict, const char *name, std::size_t &target, std::string &error)
    {
        PyObject *value = PyDict_GetItemString(dict, name);
        if (!value)
            return true;
        long long v = PyLong_AsLongLong(value);
        if (PyErr_Occurred() || v < 0)
        {
            error = std::string("Invalid integer value for ") + name;
            PyErr_Clear();
            return false;
        }
        target = static_cast<std::size_t>(v);
        return true;
    }

    bool set_bool(PyObject *dict, const char *name, bool &target, std::string &error)
    {
        PyObject *value = PyDict_GetItemString(dict, name);
        if (!value)
            return true;
        int truth = PyObject_IsTrue(value);
        if (truth == -1)
        {
            error = std::string("Invalid boolean for ") + name;
            PyErr_Clear();
            return false;
        }
        target = truth != 0;
        return true;
    }

    bool set_optional_double(PyObject *dict, const char *name, std::optional<double> &target, std::string &error)
    {
        PyObject *value = PyDict_GetItemString(dict, name);
        if (!value)
            return true;
        if (value == Py_None)
        {
            target.reset();
            return true;
        }
        double v = PyFloat_AsDouble(value);
        if (PyErr_Occurred())
        {
            error = std::string("Invalid numeric value for ") + name;
            PyErr_Clear();
            return false;
        }
        target = v;
        return true;
    }

    bool mapping_to_config(PyObject *dict, fin::app::ScenarioConfig &cfg, std::string &error)
    {
        if (!dict || !PyDict_Check(dict))
        {
            error = "Scenario config must be a dict";
            return false;
        }

        if (PyObject *ticks = PyDict_GetItemString(dict, "ticks_path"))
        {
            if (!PyUnicode_Check(ticks))
            {
                error = "ticks_path must be a string";
                return false;
            }
            cfg.ticks_path = PyUnicode_AsUTF8(ticks);
        }

        if (PyObject *tf = PyDict_GetItemString(dict, "timeframe"))
        {
            if (!PyUnicode_Check(tf))
            {
                error = "timeframe must be string";
                return false;
            }
            std::string token = PyUnicode_AsUTF8(tf);
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

        if (PyObject *model = PyDict_GetItemString(dict, "model_output_path"))
        {
            if (model == Py_None)
                cfg.model_output_path.reset();
            else if (PyUnicode_Check(model))
                cfg.model_output_path = PyUnicode_AsUTF8(model);
            else
            {
                error = "model_output_path must be string";
                return false;
            }
        }
        return true;
    }

    PyObject *config_to_dict(const fin::app::ScenarioConfig &cfg)
    {
        PyObject *dict = PyDict_New();
        dict_set(dict, "ticks_path", PyUnicode_FromString(cfg.ticks_path.c_str()));
        dict_set(dict, "timeframe", PyUnicode_FromString(timeframe_to_str(cfg.timeframe)));
        dict_set(dict, "train_ratio", PyFloat_FromDouble(cfg.train_ratio));
        dict_set(dict, "ridge_lambda", PyFloat_FromDouble(cfg.ridge_lambda));
        dict_set(dict, "ema_fast", PyLong_FromUnsignedLongLong(cfg.ema_fast));
        dict_set(dict, "ema_slow", PyLong_FromUnsignedLongLong(cfg.ema_slow));
        dict_set(dict, "rsi_period", PyLong_FromUnsignedLongLong(cfg.rsi_period));
        dict_set(dict, "macd_fast", PyLong_FromUnsignedLongLong(cfg.macd_fast));
        dict_set(dict, "macd_slow", PyLong_FromUnsignedLongLong(cfg.macd_slow));
        dict_set(dict, "macd_signal", PyLong_FromUnsignedLongLong(cfg.macd_signal));
        dict_set(dict, "rsi_buy", PyFloat_FromDouble(cfg.rsi_buy));
        dict_set(dict, "rsi_sell", PyFloat_FromDouble(cfg.rsi_sell));
        Py_INCREF(cfg.use_ema_crossover ? Py_True : Py_False);
        PyDict_SetItemString(dict, "use_ema_crossover", cfg.use_ema_crossover ? Py_True : Py_False);
        if (cfg.initial_cash)
            dict_set(dict, "initial_cash", PyFloat_FromDouble(*cfg.initial_cash));
        if (cfg.trade_qty)
            dict_set(dict, "trade_qty", PyFloat_FromDouble(*cfg.trade_qty));
        if (cfg.fee_per_trade)
            dict_set(dict, "fee_per_trade", PyFloat_FromDouble(*cfg.fee_per_trade));
        dict_set(dict, "validation_preview_limit", PyLong_FromUnsignedLongLong(cfg.validation_preview_limit));
        if (cfg.model_output_path)
            dict_set(dict, "model_output_path", PyUnicode_FromString(cfg.model_output_path->c_str()));
        return dict;
    }

    PyObject *py_run_file(PyObject *, PyObject *args)
    {
        const char *path = nullptr;
        if (!PyArg_ParseTuple(args, "s", &path))
            return nullptr;
        try
        {
            auto cfg = service().load_file(path);
            auto result = service().run(cfg);
            return scenario_result_to_dict(cfg, result);
        }
        catch (const std::exception &ex)
        {
            PyErr_SetString(PyExc_RuntimeError, ex.what());
            return nullptr;
        }
    }

    PyObject *py_load_file(PyObject *, PyObject *args)
    {
        const char *path = nullptr;
        if (!PyArg_ParseTuple(args, "s", &path))
            return nullptr;
        try
        {
            auto cfg = service().load_file(path);
            return config_to_dict(cfg);
        }
        catch (const std::exception &ex)
        {
            PyErr_SetString(PyExc_RuntimeError, ex.what());
            return nullptr;
        }
    }

    PyObject *py_run_config(PyObject *, PyObject *args, PyObject *kwargs)
    {
        PyObject *cfg_dict = nullptr;
        static const char *kwlist[] = {"config", nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O", const_cast<char **>(kwlist), &cfg_dict))
            return nullptr;

        fin::app::ScenarioConfig cfg{};
        std::string error;
        if (!mapping_to_config(cfg_dict, cfg, error))
        {
            PyErr_SetString(PyExc_ValueError, error.c_str());
            return nullptr;
        }
        if (cfg.ticks_path.empty())
        {
            PyErr_SetString(PyExc_ValueError, "config dict missing ticks_path");
            return nullptr;
        }

        try
        {
            auto result = service().run(cfg);
            return scenario_result_to_dict(cfg, result);
        }
        catch (const std::exception &ex)
        {
            PyErr_SetString(PyExc_RuntimeError, ex.what());
            return nullptr;
        }
    }
}

static PyMethodDef AiQuantMethods[] = {
    {"run_file", py_run_file, METH_VARARGS, "Run a scenario defined in a file"},
    {"run_config", reinterpret_cast<PyCFunction>(py_run_config), METH_VARARGS | METH_KEYWORDS, "Run from dictionary config"},
    {"load_file", py_load_file, METH_VARARGS, "Load scenario.ini into a dict"},
    {nullptr, nullptr, 0, nullptr}};

static struct PyModuleDef aiquant_module = {
    PyModuleDef_HEAD_INIT,
    "aiquant_api",
    "AiQuant Python bindings",
    -1,
    AiQuantMethods};

PyMODINIT_FUNC PyInit_aiquant_api(void)
{
    return PyModule_Create(&aiquant_module);
}

