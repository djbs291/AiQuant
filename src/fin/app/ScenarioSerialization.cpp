#include "fin/app/ScenarioSerialization.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

#include "fin/app/Json.hpp" // json::write_number: never emit a bare nan/inf

namespace fin::app
{
    namespace
    {
        const char *timeframe_to_cstr(fin::io::Timeframe tf)
        {
            switch (tf)
            {
            case fin::io::Timeframe::S1:
                return "S1";
            case fin::io::Timeframe::S5:
                return "S5";
            case fin::io::Timeframe::M5:
                return "M5";
            case fin::io::Timeframe::H1:
                return "H1";
            case fin::io::Timeframe::M1:
            default:
                return "M1";
            }
        }

        void append_metrics_json(std::ostream &out, const ScenarioResult &result)
        {
            out << "  \"metrics\": {\n    \"final_cash\": ";
            json::write_number(out, result.metrics.final_cash);
            out << ",\n    \"pnl\": ";
            json::write_number(out, result.metrics.pnl);
            out << ",\n    \"return_pct\": ";
            json::write_number(out, result.metrics.return_pct);
            out << ",\n    \"trades\": " << result.metrics.trades
                << ",\n    \"wins\": " << result.metrics.wins
                << ",\n    \"losses\": " << result.metrics.losses
                << ",\n    \"max_drawdown\": ";
            json::write_number(out, result.metrics.max_drawdown);
            out << "\n  },\n";
        }
    }

    std::string scenario_result_to_json(const ScenarioConfig &cfg, const ScenarioResult &result)
    {
        std::ostringstream out;
        out << "{\n";
        out << "  \"ticks_path\": " << std::quoted(cfg.ticks_path) << ",\n";
        out << "  \"timeframe\": \"" << timeframe_to_cstr(cfg.timeframe) << "\",\n";
        out << "  \"candles\": " << result.candles << ",\n";
        out << "  \"warmup_candles\": " << result.warmup_candles << ",\n";
        out << "  \"feature_rows\": " << result.feature_rows << ",\n";
        out << "  \"features\": [";
        for (std::size_t i = 0; i < result.features.size(); ++i)
        {
            if (i > 0)
                out << ", ";
            out << std::quoted(result.features[i]);
        }
        out << "],\n";
        out << "  \"model\": " << std::quoted(result.model) << ",\n";
        out << "  \"online_update\": " << (result.online_update ? "true" : "false") << ",\n";
        out << "  \"online_updates\": " << result.online_updates << ",\n";
        out << "  \"training_samples\": " << result.training.samples << ",\n";
        out << "  \"validation_samples\": " << result.validation_samples << ",\n";
        // A degenerate config (a zero period, say) can leave these NaN. They go out as null
        // rather than as a bare nan, which no strict reader accepts.
        out << "  \"training_mse\": ";
        json::write_number(out, result.training.mse);
        out << ",\n  \"validation_rmse\": ";
        json::write_number(out, result.validation_rmse);
        out << ",\n";
        // Zero unless online updating is on, where it is the predict-then-learn error over
        // the same rows validation_rmse scores with the frozen model.
        out << "  \"online_validation_rmse\": ";
        json::write_number(out, result.online_validation_rmse);
        out << ",\n";
        append_metrics_json(out, result);
        out << "  \"model_saved\": " << (result.model_saved ? "true" : "false") << ",\n";
        out << "  \"validation_preview\": [\n";
        for (std::size_t i = 0; i < result.validation_preview.size(); ++i)
        {
            const auto &row = result.validation_preview[i];
            out << "    {\"ts_ms\": " << row.ts_ms << ", \"predicted_delta\": ";
            json::write_number(out, row.predicted_delta);
            out << ", \"actual_delta\": ";
            json::write_number(out, row.actual_delta);
            out << "}";
            if (i + 1 < result.validation_preview.size())
                out << ',';
            out << "\n";
        }
        out << "  ]\n";
        out << "}\n";
        return out.str();
    }

    bool write_validation_preview_csv(const ScenarioResult &result, const std::string &path)
    {
        std::ofstream out(path);
        if (!out)
            return false;

        out << "ts_ms,predicted_delta,actual_delta\n";
        for (const auto &row : result.validation_preview)
            out << row.ts_ms << ',' << row.predicted_delta << ',' << row.actual_delta << '\n';
        return true;
    }
}
