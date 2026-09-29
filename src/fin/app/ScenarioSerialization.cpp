#include "fin/app/ScenarioSerialization.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

#include "fin/app/Json.hpp" // json::write_number: never emit a bare nan/inf

namespace fin::app
{
    namespace
    {
        void append_metrics_json(std::ostream &out, const char *key, const fin::backtest::Metrics &metrics)
        {
            out << "  \"" << key << "\": {\n    \"final_cash\": ";
            json::write_number(out, metrics.final_cash);
            out << ",\n    \"pnl\": ";
            json::write_number(out, metrics.pnl);
            out << ",\n    \"return_pct\": ";
            json::write_number(out, metrics.return_pct);
            out << ",\n    \"trades\": " << metrics.trades
                << ",\n    \"wins\": " << metrics.wins
                << ",\n    \"losses\": " << metrics.losses
                << ",\n    \"model_decisive_signals\": " << metrics.model_decisive_signals
                << ",\n    \"max_drawdown\": ";
            json::write_number(out, metrics.max_drawdown);
            out << "\n  },\n";
        }
    }

    std::string scenario_result_to_json(const ScenarioConfig &cfg, const ScenarioResult &result)
    {
        std::ostringstream out;
        out << "{\n";
        out << "  \"ticks_path\": " << std::quoted(cfg.ticks_path) << ",\n";
        // The instrument the run resolved to, and how many ticks belonged to another one and
        // were left out rather than blended into these candles.
        out << "  \"symbol\": " << std::quoted(result.symbol) << ",\n";
        out << "  \"ticks_other_symbol\": " << result.ticks_other_symbol << ",\n";
        out << "  \"timeframe\": \"" << fin::io::timeframe_token(cfg.timeframe) << "\",\n";
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
        out << "  \"model_weight\": ";
        json::write_number(out, cfg.model_weight);
        out << ",\n";
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
        // "metrics" covers only the candles the model was not trained on; the training stretch
        // is reported beside it, never folded in.
        out << "  \"in_sample_candles\": " << result.in_sample_candles << ",\n";
        out << "  \"out_of_sample_candles\": " << result.out_of_sample_candles << ",\n";
        out << "  \"out_of_sample_from_ms\": " << result.out_of_sample_from_ms << ",\n";
        append_metrics_json(out, "metrics", result.metrics);
        append_metrics_json(out, "metrics_in_sample", result.metrics_in_sample);
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
