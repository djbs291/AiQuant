#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include <charconv>
#include <fstream>
#include <chrono>
#include <exception>
#include <memory>
#include <tuple>

#include "fin/io/Pipeline.hpp"
#include "fin/backtest/Backtester.hpp"
#include "fin/indicators/FeatureBus.hpp"
#include "fin/signal/SignalEngine.hpp"
#include "fin/ml/FeatureVector.hpp"
#include "fin/ml/LinearModel.hpp"
#include "fin/ml/LinearTrainer.hpp"

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
            const std::string &tf = args[i + 1];
            if (tf == "S1")
                return fin::io::Timeframe::S1;
            if (tf == "S5")
                return fin::io::Timeframe::S5;
            if (tf == "M5")
                return fin::io::Timeframe::M5;
            if (tf == "H1")
                return fin::io::Timeframe::H1;
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

static int cmd_run_mvp(const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cerr << "Usage: aiquant run-mvp <ticks.csv> [--tf S1|S5|M1|M5|H1] [--train-ratio 0.1-0.95] [--ridge L] [--cash N] [--qty N] [--fee N] [--ema-fast N] [--ema-slow N] [--rsi N] [--macd-fast N] [--macd-slow N] [--macd-signal N] [--rsi-buy N] [--rsi-sell N] [--no-ema-xover] [--model-out path]\n";
        return 2;
    }

    const std::string path = args[0];
    fin::io::TickCsvOptions tick_opt{};
    auto tf = parse_timeframe_flag(args);
    auto res = fin::io::resample_csv_with_stats(path, tf, tick_opt);

    const std::size_t ema_fast = parse_size_flag(args, "--ema-fast").value_or(12);
    const std::size_t ema_slow = parse_size_flag(args, "--ema-slow").value_or(26);
    const std::size_t rsi_period = parse_size_flag(args, "--rsi").value_or(14);
    const std::size_t macd_fast = parse_size_flag(args, "--macd-fast").value_or(12);
    const std::size_t macd_slow = parse_size_flag(args, "--macd-slow").value_or(26);
    const std::size_t macd_signal = parse_size_flag(args, "--macd-signal").value_or(9);

    fin::indicators::FeatureBus feature_bus(ema_fast, rsi_period, macd_fast, macd_slow, macd_signal);
    std::vector<fin::indicators::FeatureRow> rows;
    rows.reserve(res.candles.size());
    for (const auto &c : res.candles)
    {
        if (auto row = feature_bus.update(c))
            rows.push_back(*row);
    }

    if (rows.size() < 3)
    {
        std::cerr << "Need at least 3 feature rows; got " << rows.size() << ". Insufficient data after indicator warmup.\n";
        return 1;
    }

    double train_ratio = parse_double_flag(args, "--train-ratio").value_or(0.7);
    if (train_ratio < 0.1)
        train_ratio = 0.1;
    if (train_ratio > 0.95)
        train_ratio = 0.95;

    std::size_t train_rows = static_cast<std::size_t>(train_ratio * static_cast<double>(rows.size()));
    if (train_rows < 2)
        train_rows = 2; // need at least two rows (one sample)
    if (train_rows >= rows.size())
        train_rows = rows.size() - 1;

    auto train_end = rows.begin() + static_cast<std::vector<fin::indicators::FeatureRow>::difference_type>(train_rows + 1);
    std::vector<fin::indicators::FeatureRow> training(rows.begin(), train_end);

    fin::ml::LinearTrainingOptions train_opts{};
    if (auto ridge = parse_double_flag(args, "--ridge"))
        train_opts.ridge_lambda = *ridge;

    fin::ml::LinearTrainingSummary summary;
    try
    {
        summary = fin::ml::train_linear_from_feature_rows(training, train_opts);
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Training failed: " << ex.what() << "\n";
        return 1;
    }

    std::size_t validation_samples = 0;
    double validation_sse = 0.0;
    std::vector<std::tuple<long long, double, double>> validation_preview;
    for (std::size_t i = train_rows; i + 1 < rows.size(); ++i)
    {
        auto fv = fin::ml::FeatureVector::from_feature_row(rows[i]);
        double pred = summary.model.predict(fv);
        double target = rows[i + 1].close - rows[i].close;
        double err = pred - target;
        validation_sse += err * err;
        ++validation_samples;

        if (validation_preview.size() < 3)
        {
            using namespace std::chrono;
            const long long ts_ms = duration_cast<milliseconds>(rows[i + 1].ts.time_since_epoch()).count();
            validation_preview.emplace_back(ts_ms, pred, target);
        }
    }

    if (auto out = parse_string_flag(args, "--model-out"))
    {
        if (!fin::ml::save_linear_model(summary.model, *out))
            std::cerr << "Warning: failed to save trained model to '" << *out << "'\n";
    }

    fin::signal::SignalEngineConfig scfg{};
    if (auto v = parse_double_flag(args, "--rsi-buy"))
        scfg.rsi_buy_below = *v;
    if (auto v = parse_double_flag(args, "--rsi-sell"))
        scfg.rsi_sell_above = *v;
    if (flag_present(args, "--no-ema-xover"))
        scfg.use_ema_crossover = false;

    fin::backtest::BacktestConfig cfg{};
    if (auto v = parse_double_flag(args, "--cash"))
        cfg.initial_cash = *v;
    if (auto v = parse_double_flag(args, "--qty"))
        cfg.trade_qty = *v;
    if (auto v = parse_double_flag(args, "--fee"))
        cfg.fee_per_trade = *v;
    cfg.ema_fast = ema_fast;
    cfg.ema_slow = ema_slow;
    cfg.rsi_period = rsi_period;

    fin::signal::SignalEngine engine{scfg};
    fin::backtest::Backtester bt(cfg, engine);

    fin::indicators::FeatureBus live_bus(ema_fast, rsi_period, macd_fast, macd_slow, macd_signal);
    std::optional<double> pending_prediction;

    const std::size_t warmup_candles = res.candles.size() - rows.size();
    for (const auto &c : res.candles)
    {
        bt.on_candle(c, pending_prediction);

        if (auto row = live_bus.update(c))
        {
            auto fv = fin::ml::FeatureVector::from_feature_row(*row);
            pending_prediction = summary.model.predict(fv);
        }
        else
        {
            pending_prediction.reset();
        }
    }

    auto metrics = bt.finalize();

    std::cout << "=== MVP pipeline ===\n";
    std::cout << "Candles (post-resample): " << res.candles.size() << "(warmup" << warmup_candles << ")\n";
    std::cout << "Feature rows: " << rows.size() << ", training samples: " << summary.samples;
    std::cout << ", validation samples: " << validation_samples << "\n";
    std::cout << "Train MSE: " << summary.mse;
    if (validation_samples > 0)
    {
        const double rmse = std::sqrt(validation_sse / static_cast<double>(validation_samples));
        std::cout << ", validation RMSE: " << rmse << '\n';
    }
    else
    {
        std::cout << ", validation RMSE: n/a\n";
    }

    const auto &named = summary.model.named_weights();
    if (!named.empty())
    {
        std::cout << "Model weights:\n";
        for (const auto &[name, weight] : named)
            std::cout << " " << name << ": " << weight << '\n';
        std::cout << " bias: " << summary.model.bias() << '\n';
    }

    if (!validation_preview.empty())
    {
        std::cout << "Validation preview (ts, pred_delta, actual_delta):\n";
        for (const auto &[ts, pred, actual] : validation_preview)
            std::cout << " " << ts << ", " << pred << ", " << actual << '\n';
    }

    std::cout << "Backtest final cash: " << metrics.final_cash << "\n";
    std::cout << "PnL: " << metrics.pnl << " (" << metrics.return_pct << "%)\n";
    std::cout << "Trades: " << metrics.trades << " (Wins: " << metrics.wins << ", Losses: " << metrics.losses << ")\n";
    std::cout << "Max DD: " << metrics.max_drawdown << "%\n";

    return 0;
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

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}
