#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <optional>
#include <charconv>
#include <fstream>
#include <chrono>
#include <exception>
#include <memory>

#include "fin/io/Pipeline.hpp"
#include "fin/backtest/Backtester.hpp"
#include "fin/indicators/FeatureBus.hpp"
#include "fin/signal/SignalEngine.hpp"
#include "fin/ml/FeatureVector.hpp"
#include "fin/ml/LinearModel.hpp"
#include "fin/ml/LinearTrainer.hpp"
#include "fin/app/ScenarioRunner.hpp"
#include "fin/app/ScenarioConfigIO.hpp"
#include "fin/app/ScenarioUtils.hpp"

using namespace fin;

// Parse string flags (e.g. output paths)
static std::optional<std::string> parse_string_flag(const std::vector<std::string> &args,
                                                    const std::string &flag)
{
    for (std::size_t i = 1; i + 1 < args.size(); ++i)
    {
        if (args[i] == flag)
        {
            return args[i + 1];
        }
    }
    return std::nullopt;
}

static std::optional<double> parse_double_flag(const std::vector<std::string> &args,
                                               const std::string &flag)
{
    for (std::size_t i = 1; i + 1 < args.size(); ++i) // start after path (args[0])
    {
        if (args[i] == flag)
        {
            const std::string &s = args[i + 1];
            double v = 0.0;
            auto *b = s.data();
            auto *e = s.data() + s.size();
            if (auto [p, ec] = std::from_chars(b, e, v); ec == std::errc{})
                return v;
        }
    }
    return std::nullopt;
}

// Presence-only flag (e.g., --no-ema-xover)
static bool flag_present(const std::vector<std::string> &args, const std::string &flag)
{
    for (std::size_t i = 1; i < args.size(); ++i)
    {
        if (args[i] == flag)
            return true;
    }
    return false;
}

static std::optional<std::size_t> parse_size_flag(const std::vector<std::string> &args,
                                                  const std::string &flag)
{
    for (std::size_t i = 1; i + 1 < args.size(); ++i)
    {
        if (args[i] == flag)
        {
            const std::string &s = args[i + 1];
            std::size_t v = 0;
            auto *b = s.data();
            auto *e = s.data() + s.size();
            if (auto [p, ec] = std::from_chars(b, e, v); ec == std::errc{})
                return v;
        }
    }
    return std::nullopt;
}

static fin::io::Timeframe parse_timeframe_flag(const std::vector<std::string> &args)
{
    for (std::size_t i = 1; i + 1 < args.size(); ++i)
    {
        if (args[i] == "--tf")
        {
            if (auto parsed = fin::app::parse_timeframe_token(args[i + 1]))
                return *parsed;
            return fin::io::Timeframe::M1;
        }
    }
    return fin::io::Timeframe::M1;
}

static int cmd_backtest(const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cerr << "Usage: aiquant backtest <ticks.csv> [--tf S1|S5|M1|M5|H1] [--cash N] [--qty N] [--fee N] [--ema-fast N] [--ema-slow N] [--rsi N] [--macd-fast N] [--macd-slow N] [--macd-signal N] [--rsi-buy N] [--rsi-sell N] [--no-ema-xover] [--candles-out path] [--model-linear path]\n";
        return 2;
    }

    const std::string path = args[0];

    fin::io::TickCsvOptions opt{}; // defaults: header, epoch-ms
    auto tf = parse_timeframe_flag(args);
    auto res = fin::io::resample_csv_with_stats(path, tf, opt);

    fin::backtest::BacktestConfig cfg{}; // defaults
    if (auto v = parse_double_flag(args, "--cash"))
        cfg.initial_cash = *v;
    if (auto v = parse_double_flag(args, "--qty"))
        cfg.trade_qty = *v;
    if (auto v = parse_double_flag(args, "--fee"))
        cfg.fee_per_trade = *v;
    if (auto v = parse_size_flag(args, "--ema-fast"))
        cfg.ema_fast = *v;
    if (auto v = parse_size_flag(args, "--ema-slow"))
        cfg.ema_slow = *v;
    if (auto v = parse_size_flag(args, "--rsi"))
        cfg.rsi_period = *v;
    const std::size_t macd_fast = parse_size_flag(args, "--macd-fast").value_or(12);
    const std::size_t macd_slow = parse_size_flag(args, "--macd-slow").value_or(26);
    const std::size_t macd_signal = parse_size_flag(args, "--macd-signal").value_or(9);

    std::unique_ptr<fin::indicators::FeatureBus> feature_bus;
    std::optional<fin::ml::LinearModel> linear_model;
    if (auto model_path = parse_string_flag(args, "--model-linear"))
    {
        fin::ml::LinearModel loaded;
        if (!loaded.load_from_file(*model_path))
        {
            std::cerr << "Failed to load linear model configuration: " << *model_path << "\n";
        }
        else
        {
            linear_model = std::move(loaded);
            feature_bus = std::make_unique<fin::indicators::FeatureBus>(cfg.ema_fast, cfg.rsi_period, macd_fast, macd_slow, macd_signal);
        }
    }
    // Signal config (MVP): RSI Thresholds and EMA crossover on/off
    fin::signal::SignalEngineConfig scfg{}; // defaults: buy <= 30, sell >= 70, use EMA crossover
    if (auto v = parse_double_flag(args, "--rsi_buy"))
        scfg.rsi_buy_below = *v;
    if (auto v = parse_double_flag(args, "--rsi-sell"))
        scfg.rsi_sell_above = *v;
    if (flag_present(args, "--no-ema-xover"))
        scfg.use_ema_crossover = false;

    fin::signal::SignalEngine eng{scfg}; // defaults
    fin::backtest::Backtester bt(cfg, eng);

    for (const auto &c : res.candles)
    {
        std::optional<double> prediction;
        if (feature_bus)
        {
            if (auto row = feature_bus->update(c))
            {
                auto fv = fin::ml::FeatureVector::from_feature_row(*row);
                try
                {
                    if (linear_model)
                        prediction = linear_model->predict(fv);
                }
                catch (const std::exception &ex)
                {
                    std::cerr << "Linear model prediction failed: " << ex.what() << "\n";
                }
            }
        }

        bt.on_candle(c, prediction);
    }
    auto m = bt.finalize();

    std::cout << "Candles: " << res.candles.size() << "\n";
    std::cout << "Rows: " << res.stats.rows << ", Parsed: " << res.stats.parsed << ", Skipped: " << res.stats.skipped << "\n";
    std::cout << "Final Cash: " << m.final_cash << "\n";
    std::cout << "PnL: " << m.pnl << " (" << m.return_pct << "%)\n";
    std::cout << "Max DD: " << m.max_drawdown << "%\n";
    std::cout << "Trades: " << m.trades << ", Wins: " << m.wins << ", Losses: " << m.losses << "\n";

    // Optional: export resampled candles to CSV
    if (auto outp = parse_string_flag(args, "--candles-out"))
    {
        std::ofstream ofs(*outp);
        if (!ofs)
        {
            std::cerr << "Failed to open --candles-out file: " << *outp << "\n";
        }
        else
        {
            auto to_ms = [](const fin::core::Timestamp &ts) -> long long
            {
                using namespace std::chrono;
                return duration_cast<milliseconds>(ts.time_since_epoch()).count();
            };
            ofs << "Timestamp,open,high,low,close,volume\n";
            for (const auto &c : res.candles)
            {
                ofs << to_ms(c.start_time()) << ',' << c.open().value() << ',' << c.high().value() << ',' << c.low().value() << ',' << c.close().value() << ',' << c.volume().value() << "\n";
            }
        }
    }

    return 0;
}

static int cmd_train_linear(const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cerr << "Usage: aiquant train-linear <ticks.csv> [--tf S1|S5|M1|M5|H1] [--ema-fast N] [--rsi N] [--macd-fast N] [--macd-slow N] [--macd-signal N] [--out path]\n";
        return 2;
    }

    const std::string path = args[0];
    fin::io::TickCsvOptions opt{};
    auto tf = parse_timeframe_flag(args);
    auto res = fin::io::resample_csv_with_stats(path, tf, opt);

    std::size_t ema_fast = parse_size_flag(args, "--ema-fast").value_or(12);
    std::size_t rsi_period = parse_size_flag(args, "--rsi").value_or(14);
    std::size_t macd_fast = parse_size_flag(args, "--macd-fast").value_or(12);
    std::size_t macd_slow = parse_size_flag(args, "--macd-slow").value_or(26);
    std::size_t macd_signal = parse_size_flag(args, "--macd-signal").value_or(9);

    fin::indicators::FeatureBus fb(ema_fast, rsi_period, macd_fast, macd_slow, macd_signal);
    std::vector<fin::indicators::FeatureRow> rows;
    rows.reserve(res.candles.size());
    for (const auto &c : res.candles)
        if (auto row = fb.update(c))
            rows.push_back(*row);

    if (rows.size() < 2)
    {
        std::cerr << "Insufficient feature rows for training (need >= 2)." << std::endl;
        return 1;
    }
    fin::ml::LinearTrainingOptions train_opt{};
    fin::ml::LinearTrainingSummary summary{};
    try
    {
        summary = fin::ml::train_linear_from_feature_rows(rows, train_opt);
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Training failed: " << ex.what() << std::endl;
        return 1;
    }

    const std::string out_path = parse_string_flag(args, "--out").value_or("linear_model.csv");
    if (!fin::ml::save_linear_model(summary.model, out_path))
    {
        std::cerr << "Failed to write model to: " << out_path << std::endl;
        return 1;
    }

    std::cout << "Samples: " << summary.samples << "\n";
    std::cout << "Training MSE: " << summary.mse << "\n";
    std::cout << "Saved linear model to: " << out_path << "\n";
    return 0;
}

// MVP: emit features computed from resampled candles to stdou (CSV)
static int cmd_features(const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cerr << "Usage: aiquant features <ticks.csv> [--tf S1|S5|M1|M5|H1] [--ema-fast N] [--rsi N] [--macd-fast N] [--macd-slow N] [--macd-signal N]\n";
        return 2;
    }
    const std::string path = args[0];

    fin::io::TickCsvOptions opt{};
    auto tf = parse_timeframe_flag(args);
    auto res = fin::io::resample_csv_with_stats(path, tf, opt);

    std::size_t ema_fast = parse_size_flag(args, "--ema-fast").value_or(12);
    std::size_t rsi_period = parse_size_flag(args, "--rsi").value_or(14);
    std::size_t macd_fast = parse_size_flag(args, "--macd-fast").value_or(12);
    std::size_t macd_slow = parse_size_flag(args, "--macd-slow").value_or(26);
    std::size_t macd_signal = parse_size_flag(args, "--macd-signal").value_or(9);

    fin::indicators::FeatureBus fb(ema_fast, rsi_period, macd_fast, macd_slow, macd_signal);

    // Header
    std::cout << "Timestamp, close, ema_fast, rsi, macd, macd_signal, macd_hist\n";
    auto to_ms = [](const fin::core::Timestamp &ts) -> long long
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(ts.time_since_epoch()).count();
    };

    for (const auto &c : res.candles)
    {
        if (auto row = fb.update(c))
        {
            std::cout << to_ms(row->ts) << ',' << row->close << ',' << row->ema_fast << ',' << row->rsi << ',' << row->macd << ',' << row->macd_signal << ',' << row->macd_hist << "\n";
        }
    }
    return 0;
}

static const char *timeframe_to_cstr(fin::io::Timeframe tf)
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

static void print_scenario_result(const fin::app::ScenarioConfig &cfg, const fin::app::ScenarioResult &result)
{
    std::cout << "=== MVP scenario ===\n";
    std::cout << "Ticks: " << cfg.ticks_path << "\n";
    std::cout << "Timeframe: " << timeframe_to_cstr(cfg.timeframe) << "\n";
    std::cout << "Candles (post-resample): " << result.candles;
    if (result.warmup_candles > 0)
        std::cout << " (warmup " << result.warmup_candles << ")";
    std::cout << "\n";
    std::cout << "Feature rows: " << result.feature_rows << ", training samples: " << result.training.samples
              << ", validation samples: " << result.validation_samples << "\n";
    std::cout << "Train MSE: " << result.training.mse;
    if (result.validation_samples > 0)
        std::cout << ", validation RMSE: " << result.validation_rmse << "\n";
    else
        std::cout << ", validation RMSE: n/a\n";

    const auto &named = result.training.model.named_weights();
    if (!named.empty())
    {
        std::cout << "Model weights: \n";
        for (const auto &[name, weight] : named)
            std::cout << " " << name << ": " << weight << "\n";
        std::cout << " bias: " << result.training.model.bias() << "\n";
    }

    if (!result.validation_preview.empty())
    {
        std::cout << "Validation preview (ts_ms, pred_delta, actual_delta):\n";
        for (const auto &row : result.validation_preview)
            std::cout << " " << row.ts_ms << ", " << row.predicted_delta << ", " << row.actual_delta << "\n";
    }

    std::cout << "Backtest final cash: " << result.metrics.final_cash << "\n";
    std::cout << "PnL: " << result.metrics.pnl << " (" << result.metrics.return_pct << "%)\n";
    std::cout << "Trades: " << result.metrics.trades << " (Wins: " << result.metrics.wins
              << ", Losses: " << result.metrics.losses << ")\n";
    std::cout << "Max DD: " << result.metrics.max_drawdown << "%\n";
    if (result.model_saved && cfg.model_output_path)
        std::cout << "Saved model: " << *cfg.model_output_path << "\n";
}

static void print_scenario_result_json(const fin::app::ScenarioConfig &cfg, const fin::app::ScenarioResult &result)
{
    std::cout << "{\n";
    std::cout << "  \"ticks_path\": " << std::quoted(cfg.ticks_path) << ",\n";
    std::cout << "  \"timeframe\": \"" << timeframe_to_cstr(cfg.timeframe) << "\",\n";
    std::cout << "  \"candles\": " << result.candles << ",\n";
    std::cout << "  \"warmup_candles\": " << result.warmup_candles << ",\n";
    std::cout << "  \"feature_rows\": " << result.feature_rows << ",\n";
    std::cout << "  \"training_samples\": " << result.training.samples << ",\n";
    std::cout << "  \"validation_samples\": " << result.validation_samples << ",\n";
    std::cout << "  \"training_mse\": " << result.training.mse << ",\n";
    std::cout << "  \"validation_rmse\": " << result.validation_rmse << ",\n";
    std::cout << "  \"pnl\": " << result.metrics.pnl << ",\n";
    std::cout << "  \"return_pct\": " << result.metrics.return_pct << ",\n";
    std::cout << "  \"trades\": " << result.metrics.trades << ",\n";
    std::cout << "  \"wins\": " << result.metrics.wins << ",\n";
    std::cout << "  \"losses\": " << result.metrics.losses << ",\n";
    std::cout << "  \"max_drawdown\": " << result.metrics.max_drawdown << ",\n";
    std::cout << "  \"model_saved\": " << std::boolalpha << result.model_saved << std::noboolalpha << ",\n";
    std::cout << "  \"validation_preview\": [\n";
    for (std::size_t i = 0; i < result.validation_preview.size(); ++i)
    {
        const auto &row = result.validation_preview[i];
        std::cout << "    {\"ts_ms\": " << row.ts_ms
                  << ", \"predicted\": " << row.predicted_delta
                  << ", \"actual\": " << row.actual_delta << "}";
        if (i + 1 < result.validation_preview.size())
            std::cout << ',';
        std::cout << "\n";
    }
    std::cout << "  ]\n";
    std::cout << "}\n";
}

static bool write_validation_preview_csv(const fin::app::ScenarioResult &result, const std::string &path)
{
    std::ofstream out(path);
    if (!out)
        return false;

    out << "ts_ms,predicted_delta,actual_delta\n";
    for (const auto &row : result.validation_preview)
        out << row.ts_ms << ',' << row.predicted_delta << ',' << row.actual_delta << '\n';
    return true;
}

static int cmd_run_mvp(const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cerr << "Usage: aiquant run-mvp <ticks.csv> [--tf S1|S5|M1|M5|H1] [--train-ratio 0.1-0.95] [--ridge L] [--cash N] [--qty N] [--fee N] [--ema-fast N] [--ema-slow N] [--rsi N] [--macd-fast N] [--macd-slow N] [--macd-signal N] [--rsi-buy N|--rsi_buy N] [--rsi-sell N|--rsi_sell N] [--no-ema-xover] [--preview N] [--preview-out path] [--model-out path] [--json]\n";
        return 2;
    }

    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = args[0];
    cfg.timeframe = parse_timeframe_flag(args);

    if (auto ratio = parse_double_flag(args, "--train-ratio"))
        cfg.train_ratio = *ratio;
    if (auto ridge = parse_double_flag(args, "--ridge"))
        cfg.ridge_lambda = *ridge;

    if (auto v = parse_size_flag(args, "--ema-fast"))
        cfg.ema_fast = *v;
    if (auto v = parse_size_flag(args, "--ema-slow"))
        cfg.ema_slow = *v;
    if (auto v = parse_size_flag(args, "--rsi"))
        cfg.rsi_period = *v;
    if (auto v = parse_size_flag(args, "--macd-fast"))
        cfg.macd_fast = *v;
    if (auto v = parse_size_flag(args, "--macd-slow"))
        cfg.macd_slow = *v;
    if (auto v = parse_size_flag(args, "--macd-signal"))
        cfg.macd_signal = *v;
    if (auto v = parse_size_flag(args, "--preview"))
        cfg.validation_preview_limit = *v;

    if (auto v = parse_double_flag(args, "--rsi-buy"))
        cfg.rsi_buy = *v;
    else if (auto v_alt = parse_double_flag(args, "--rsi_buy"))
        cfg.rsi_buy = *v_alt;

    if (auto v = parse_double_flag(args, "--rsi-sell"))
        cfg.rsi_sell = *v;
    else if (auto v_alt = parse_double_flag(args, "--rsi_sell"))
        cfg.rsi_sell = *v_alt;

    cfg.use_ema_crossover = !flag_present(args, "--no-ema-xover");

    if (auto v = parse_double_flag(args, "--cash"))
        cfg.initial_cash = *v;
    if (auto v = parse_double_flag(args, "--qty"))
        cfg.trade_qty = *v;
    if (auto v = parse_double_flag(args, "--fee"))
        cfg.fee_per_trade = *v;

    if (auto out = parse_string_flag(args, "--model-out"))
        cfg.model_output_path = *out;

    auto preview_out = parse_string_flag(args, "--preview-out");
    bool json_output = flag_present(args, "--json");

    try
    {
        auto result = fin::app::run_scenario(cfg);
        print_scenario_result(cfg, result);
        if (json_output)
            print_scenario_result_json(cfg, result);
        if (preview_out && !write_validation_preview_csv(result, *preview_out))
            std::cerr << "Failed to write validation preview to '" << *preview_out << "'\n";
        return 0;
    }
    catch (const std::exception &ex)
    {
        std::cerr << "run-mvp failed: " << ex.what() << "\n";
        return 1;
    }
}

static int cmd_run_config(const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cerr << "Usage: aiquant run-config <scenario.ini> [--preview-out path] [--json]\n";
        return 2;
    }

    auto preview_out = parse_string_flag(args, "--preview-out");
    bool json_output = flag_present(args, "--json");

    fin::app::ScenarioConfig cfg{};
    std::string error;
    if (!load_scenario_file(args[0], cfg, error))
    {
        std::cerr << error << "\n";
        return 1;
    }

    try
    {
        auto result = fin::app::run_scenario(cfg);
        print_scenario_result(cfg, result);
        if (json_output)
            print_scenario_result_json(cfg, result);
        if (preview_out && !write_validation_preview_csv(result, *preview_out))
            std::cerr << "Failed to write validation preview to '" << *preview_out << "'\n";
        return 0;
    }
    catch (const std::exception &ex)
    {
        std::cerr << "run-config failed: " << ex.what() << '\n';
        return 1;
    }
}

int main(int argc, char **argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty())
    {
        std::cout << "AiQuant CLI (MVP)\n";
        std::cout << "Commands: \n";
        std::cout << "  backtest <ticks.csv> [--tf S1|S5|M1|M5|H1] [--cash N] [--qty N] [--fee N] [--ema-fast N] [--ema-slow N] [--rsi N] [--macd-fast N] [--macd-slow N] [--macd-signal N] [--rsi-buy N] [--rsi-sell N] [--no-ema-xover] [--candles-out path] [--model-linear path]\n";
        std::cout << "  features <ticks.csv> [--tf S1|S5|M1|M5|H1] [--ema-fast N] [--rsi N] [--macd-fast N] [--macd-slow N] [--macd-signal N]\n";
        std::cout << "    Resample candles and run RSI+EMA strategy\n";
        std::cout << "  train-linear <ticks.csv> [--tf ...] [--ema-fast N] [--rsi N] [--macd-fast N] [--macd-slow N] [--macd-signal N] [--out path]\n";
        std::cout << "  run-mvp <ticks.csv> [end-to-end training + signal backtest]\n";
        std::cout << "  run-config <scenario.ini> [execute configuration-driven scenario]\n";

        return 0;
    }

    const std::string cmd = args[0];
    if (cmd == "backtest")
    {
        return cmd_backtest({args.begin() + 1, args.end()});
    }
    if (cmd == "features")
    {
        return cmd_features({args.begin() + 1, args.end()});
    }
    if (cmd == "train-linear")
    {
        return cmd_train_linear({args.begin() + 1, args.end()});
    }
    if (cmd == "run-mvp")
    {
        return cmd_run_mvp({args.begin() + 1, args.end()});
    }
    if (cmd == "run-config")
    {
        return cmd_run_config({args.begin() + 1, args.end()});
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}
