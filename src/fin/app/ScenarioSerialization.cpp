#include "fin/app/ScenarioSerialization.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

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
            out << "  \"metrics\": {\n"
                << "    \"final_cash\": " << result.metrics.final_cash << ",\n"
                << "    \"pnl\": " << result.metrics.pnl << ",\n"
                << "    \"return_pct\": " << result.metrics.return_pct << ",\n"
                << "    \"trades\": " << result.metrics.trades << ",\n"
                << "    \"wins\": " << result.metrics.wins << ",\n"
                << "    \"losses\": " << result.metrics.losses << ",\n"
                << "    \"max_drawdown\": " << result.metrics.max_drawdown << "\n"
                << "  },\n";
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
        out << "  \"training_samples\": " << result.training.samples << ",\n";
        out << "  \"validation_samples\": " << result.validation_samples << ",\n";
        out << "  \"training_mse\": " << result.training.mse << ",\n";
        out << "  \"validation_rmse\": " << result.validation_rmse << ",\n";
        append_metrics_json(out, result);
        out << "  \"model_saved\": " << (result.model_saved ? "true" : "false") << ",\n";
        out << "  \"validation_preview\": [\n";
        for (std::size_t i = 0; i < result.validation_preview.size(); ++i)
        {
            const auto &row = result.validation_preview[i];
            out << "    {\"ts_ms\": " << row.ts_ms
                << ", \"predicted_delta\": " << row.predicted_delta
                << ", \"actual_delta\": " << row.actual_delta << "}";
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
