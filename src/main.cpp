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
#include "fin/app/ScenarioSerialization.hpp"
#include "fin/stream/StreamEngine.hpp"

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

// Comma-separated feature set, e.g. --features close,ema_fast,rsi,atr. Empty when the flag is
// absent, which every caller reads as "the historical six". Unknown names are rejected by the
// FeatureBus, which reports them through the caller's catch.
static std::vector<std::string> parse_feature_list(const std::vector<std::string> &args)
{
    std::vector<std::string> features;

    auto flag = parse_string_flag(args, "--features");
    if (!flag)
        return features;

    const std::string &list = *flag;
    std::size_t start = 0;
    while (start <= list.size())
    {
        const std::size_t comma = list.find(',', start);
        const std::size_t end = (comma == std::string::npos) ? list.size() : comma;
        std::string name = list.substr(start, end - start);
        const auto first = name.find_first_not_of(" \t");
        const auto last = name.find_last_not_of(" \t");
        if (first != std::string::npos)
        {
            name = name.substr(first, last - first + 1);
            // Lowercase to match the INI parser, so --features RSI and rsi behave alike.
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch)
                           { return static_cast<char>(std::tolower(ch)); });
            features.push_back(std::move(name));
        }
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }

    return features;
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

    // Header follows the bus schema, so it stays correct whatever the feature set is.
    std::cout << "Timestamp";
    for (const auto &name : fb.schema().names)
        std::cout << ", " << name;
    std::cout << "\n";
    auto to_ms = [](const fin::core::Timestamp &ts) -> long long
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(ts.time_since_epoch()).count();
    };

    for (const auto &c : res.candles)
    {
        if (auto row = fb.update(c))
        {
            std::cout << to_ms(row->ts);
            for (double value : row->values)
                std::cout << ',' << value;
            std::cout << "\n";
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

// Writes the human-readable report to `os`. With --json the report goes to stderr so that
// stdout carries nothing but the JSON document and stays pipeable into jq.
static void print_scenario_result(const fin::app::ScenarioConfig &cfg, const fin::app::ScenarioResult &result,
                                  std::ostream &os)
{
    os << "=== MVP scenario ===\n";
    os << "Ticks: " << cfg.ticks_path << "\n";
    os << "Timeframe: " << timeframe_to_cstr(cfg.timeframe) << "\n";
    os << "Candles (post-resample): " << result.candles;
    if (result.warmup_candles > 0)
        os << " (warmup " << result.warmup_candles << ")";
    os << "\n";
    os << "Feature rows: " << result.feature_rows << ", training samples: " << result.training.samples
              << ", validation samples: " << result.validation_samples << "\n";
    os << "Train MSE: " << result.training.mse;
    if (result.validation_samples > 0)
        os << ", validation RMSE: " << result.validation_rmse << "\n";
    else
        os << ", validation RMSE: n/a\n";

    os << "Model: " << result.model;
    if (result.online_update)
    {
        // The frozen RMSE above and this one score the same rows: one with the model as
        // trained, one with the model as it keeps learning.
        os << " (online: " << result.online_updates << " updates, prequential RMSE "
           << result.online_validation_rmse << ")";
    }
    os << "\n";

    const auto &named = result.training.model.named_weights();
    if (!named.empty())
    {
        os << "Model weights: \n";
        for (const auto &[name, weight] : named)
            os << " " << name << ": " << weight << "\n";
        os << " bias: " << result.training.model.bias() << "\n";
    }

    if (!result.validation_preview.empty())
    {
        os << "Validation preview (ts_ms, pred_delta, actual_delta):\n";
        for (const auto &row : result.validation_preview)
            os << " " << row.ts_ms << ", " << row.predicted_delta << ", " << row.actual_delta << "\n";
    }

    os << "Backtest final cash: " << result.metrics.final_cash << "\n";
    os << "PnL: " << result.metrics.pnl << " (" << result.metrics.return_pct << "%)\n";
    os << "Trades: " << result.metrics.trades << " (Wins: " << result.metrics.wins
              << ", Losses: " << result.metrics.losses << ")\n";
    os << "Max DD: " << result.metrics.max_drawdown << "%\n";
    if (result.model_saved && cfg.model_output_path)
        os << "Saved model: " << *cfg.model_output_path << "\n";
}

static int cmd_run_mvp(const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cerr << "Usage: aiquant run-mvp <ticks.csv> [--tf S1|S5|M1|M5|H1] [--train-ratio 0.1-0.95] [--ridge L] [--model ridge|sgd] [--sgd-lr N] [--sgd-l2 N] [--sgd-epochs N] [--sgd-power-t N] [--no-sgd-standardize] [--online] [--cash N] [--qty N] [--fee N] [--ema-fast N] [--ema-slow N] [--rsi N] [--macd-fast N] [--macd-slow N] [--macd-signal N] [--rsi-buy N|--rsi_buy N] [--rsi-sell N|--rsi_sell N] [--no-ema-xover] [--preview N] [--preview-out path] [--model-out path] [--features a,b,c] [--json]\n";
        return 2;
    }

    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = args[0];
    cfg.timeframe = parse_timeframe_flag(args);

    if (auto ratio = parse_double_flag(args, "--train-ratio"))
        cfg.train_ratio = *ratio;
    if (auto ridge = parse_double_flag(args, "--ridge"))
        cfg.ridge_lambda = *ridge;

    if (auto model = parse_string_flag(args, "--model"))
    {
        // Lowercased like the INI value, so --model SGD and --model sgd agree.
        std::string token = *model;
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
        if (token == "sgd")
            cfg.model = fin::app::ModelKind::Sgd;
        else if (token == "ridge" || token == "linear")
            cfg.model = fin::app::ModelKind::Ridge;
        else
        {
            std::cerr << "Unknown --model '" << *model << "' (expected ridge or sgd)\n";
            return 2;
        }
    }
    if (auto v = parse_double_flag(args, "--sgd-lr"))
        cfg.sgd.learning_rate = *v;
    if (auto v = parse_double_flag(args, "--sgd-l2"))
        cfg.sgd.l2 = *v;
    if (auto v = parse_size_flag(args, "--sgd-epochs"))
        cfg.sgd.epochs = *v;
    if (auto v = parse_double_flag(args, "--sgd-power-t"))
        cfg.sgd.power_t = *v;
    if (flag_present(args, "--no-sgd-standardize"))
        cfg.sgd.standardize = false;
    // Requires --model sgd; run_scenario refuses the combination rather than ignoring it.
    if (flag_present(args, "--online"))
        cfg.online_update = true;

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

    cfg.features = parse_feature_list(args);

    if (auto out = parse_string_flag(args, "--model-out"))
        cfg.model_output_path = *out;

    auto preview_out = parse_string_flag(args, "--preview-out");
    bool json_output = flag_present(args, "--json");

    try
    {
        auto result = fin::app::run_scenario(cfg);
        print_scenario_result(cfg, result, json_output ? std::cerr : std::cout);
        if (json_output)
            std::cout << fin::app::scenario_result_to_json(cfg, result);
        if (preview_out && !fin::app::write_validation_preview_csv(result, *preview_out))
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
        print_scenario_result(cfg, result, json_output ? std::cerr : std::cout);
        if (json_output)
            std::cout << fin::app::scenario_result_to_json(cfg, result);
        if (preview_out && !fin::app::write_validation_preview_csv(result, *preview_out))
            std::cerr << "Failed to write validation preview to '" << *preview_out << "'\n";
        return 0;
    }
    catch (const std::exception &ex)
    {
        std::cerr << "run-config failed: " << ex.what() << '\n';
        return 1;
    }
}

// Prints one CSV row per signal as the stream produces it. stdout stays pure data, as in
// `features`, so a live stream pipes; the summary goes to stderr.
class CsvSignalSink final : public fin::stream::ISignalSink
{
public:
    CsvSignalSink(bool print_holds, std::size_t limit)
        : print_holds_(print_holds), limit_(limit) {}

    void on_signal(const fin::stream::StreamEvent &event) override
    {
        if (!print_holds_ && event.signal.type == fin::signal::SignalType::Hold)
            return;
        if (limit_ > 0 && printed_ >= limit_)
            return;

        using namespace std::chrono;
        const long long ts_ms =
            duration_cast<milliseconds>(event.candle.start_time().time_since_epoch()).count();

        std::cout << ts_ms << ',' << event.symbol << ',' << signal_to_cstr(event.signal.type)
                  << ',' << event.signal.score << ',' << event.candle.close().value() << ',';
        if (event.prediction)
            std::cout << *event.prediction; // empty during warmup
        std::cout << ',' << (event.partial ? 1 : 0) << ',' << event.signal.source << "\n";
        ++printed_;
    }

    [[nodiscard]] std::size_t printed() const noexcept { return printed_; }

private:
    static const char *signal_to_cstr(fin::signal::SignalType type)
    {
        switch (type)
        {
        case fin::signal::SignalType::Buy:
            return "Buy";
        case fin::signal::SignalType::Sell:
            return "Sell";
        case fin::signal::SignalType::Hold:
        default:
            return "Hold";
        }
    }

    bool print_holds_ = false;
    std::size_t limit_ = 0;
    std::size_t printed_ = 0;
};

static int cmd_stream(const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cerr << "Usage: aiquant stream <ticks.csv> [--tf S1|S5|M1|M5|H1] [--model-linear path] [--features a,b,c] [--symbol SYM] [--ema-fast N] [--ema-slow N] [--rsi N] [--macd-fast N] [--macd-slow N] [--macd-signal N] [--rsi-buy N] [--rsi-sell N] [--no-ema-xover] [--all] [--limit N]\n";
        return 2;
    }

    const std::string path = args[0];

    fin::stream::StreamConfig cfg{};
    cfg.timeframe = parse_timeframe_flag(args);

    if (auto v = parse_size_flag(args, "--ema-fast"))
        cfg.params.ema_fast = *v;
    if (auto v = parse_size_flag(args, "--ema-slow"))
        cfg.params.ema_slow = *v;
    if (auto v = parse_size_flag(args, "--rsi"))
        cfg.params.rsi = *v;
    if (auto v = parse_size_flag(args, "--macd-fast"))
        cfg.params.macd_fast = *v;
    if (auto v = parse_size_flag(args, "--macd-slow"))
        cfg.params.macd_slow = *v;
    if (auto v = parse_size_flag(args, "--macd-signal"))
        cfg.params.macd_signal = *v;

    if (auto v = parse_double_flag(args, "--rsi-buy"))
        cfg.signal.rsi_buy_below = *v;
    if (auto v = parse_double_flag(args, "--rsi-sell"))
        cfg.signal.rsi_sell_above = *v;
    cfg.signal.use_ema_crossover = !flag_present(args, "--no-ema-xover");

    if (auto symbol = parse_string_flag(args, "--symbol"))
    {
        // Naming a symbol is how you say "this file holds several; take mine and skip the
        // rest". Without it, a second symbol is an error rather than a silent blend.
        cfg.symbol = *symbol;
        cfg.foreign_symbol = fin::stream::SymbolPolicy::Skip;
    }

    cfg.features = parse_feature_list(args);

    std::shared_ptr<fin::ml::IModel> model;
    if (auto model_path = parse_string_flag(args, "--model-linear"))
    {
        auto loaded = std::make_shared<fin::ml::LinearModel>();
        if (!loaded->load_from_file(*model_path))
        {
            std::cerr << "Failed to load linear model: " << *model_path << "\n";
            return 1;
        }
        // Without an explicit --features, take the set the model file recorded. Feeding a
        // model a feature set it was not trained on is the mistake this closes.
        if (cfg.features.empty() && !loaded->feature_names().empty())
            cfg.features = loaded->feature_names();
        model = std::move(loaded);
    }

    CsvSignalSink sink(flag_present(args, "--all"), parse_size_flag(args, "--limit").value_or(0));
    fin::stream::StreamEngine engine(cfg, model, &sink);

    fin::io::TickCsvOptions opt{};
    fin::io::FileTickSource source(path, opt);

    std::cout << "Timestamp,symbol,signal,score,close,prediction,partial,reason\n";

    fin::stream::StreamStats stats{};
    try
    {
        stats = engine.run(source);
    }
    catch (const std::exception &ex)
    {
        std::cerr << "stream failed: " << ex.what() << "\n";
        return 1;
    }

    // The header is parsed on the first next(), so a missing column is only known once the
    // run is over. Without this the stream would report a tidy zero of everything.
    if (!source.error().empty())
    {
        std::cerr << "stream failed: " << source.error() << "\n";
        return 1;
    }

    const auto &read = source.stats();
    std::cerr << "=== stream ===\n";
    // Deliberately not ReadStats::rows: it counts the header line too, so reporting it beside
    // "skipped 0" would imply a tick went missing when none did.
    std::cerr << "Ticks: " << read.parsed << " parsed, " << read.skipped << " unparsable, "
              << stats.ticks_out_of_order << " out of order, "
              << stats.ticks_other_symbol << " other symbol\n";
    std::cerr << "Symbol: " << engine.bound_symbol() << "\n";
    std::cerr << "Candles: " << stats.candles << ", feature rows: " << stats.feature_rows
              << ", predictions: " << stats.predictions;
    if (stats.prediction_errors > 0)
        std::cerr << " (" << stats.prediction_errors << " failed)";
    std::cerr << "\n";
    std::cerr << "Signals: " << stats.signals << " (Buy " << stats.buys << ", Sell " << stats.sells
              << ", Hold " << stats.holds << ") - printed " << sink.printed() << "\n";
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
        std::cout << "  run-config <scenario.ini> [execute configuration-driven scenario]\n";
        std::cout << "  stream <ticks.csv> [live pipeline: ticks -> candles -> model -> signals]\n";

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
    if (cmd == "stream")
    {
        return cmd_stream({args.begin() + 1, args.end()});
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}
