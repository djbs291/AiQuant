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
#include <sstream>
#include <unordered_map>
#include <utility>

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

// The indicator flags a user can type, and the FeatureParams key each one sets.
static const std::pair<const char *, const char *> kParamFlags[] = {
    {"--ema-fast", "ema_fast"}, {"--ema-slow", "ema_slow"}, {"--rsi", "rsi"},
    {"--macd-fast", "macd_fast"}, {"--macd-slow", "macd_slow"}, {"--macd-signal", "macd_signal"},
};

// --tf is only an override when it is typed, and a token that is not a timeframe is an error
// rather than the M1 that parse_timeframe_flag quietly falls back to.
static bool explicit_timeframe(const std::vector<std::string> &args, std::optional<fin::io::Timeframe> &out)
{
    for (std::size_t i = 1; i + 1 < args.size(); ++i)
    {
        if (args[i] == "--tf")
        {
            out = fin::app::parse_timeframe_token(args[i + 1]);
            if (!out)
            {
                std::cerr << "Unknown --tf '" << args[i + 1] << "' (expected S1, S5, M1, M5 or H1)\n";
                return false;
            }
            return true;
        }
    }
    out.reset();
    return true;
}

// What the user typed, as opposed to what defaulted: only these can contradict a model file.
static fin::stream::ModelOverrides explicit_overrides(const std::vector<std::string> &args,
                                                      std::vector<std::string> features,
                                                      std::optional<fin::io::Timeframe> timeframe)
{
    fin::stream::ModelOverrides overrides;
    overrides.features = std::move(features);
    overrides.timeframe = timeframe;
    for (const auto &[flag, key] : kParamFlags)
    {
        if (auto v = parse_size_flag(args, flag))
            overrides.params.emplace_back(key, static_cast<double>(*v));
    }
    return overrides;
}

// "tf M5, rsi=10, atr=7": what a model file made the run adopt, for the summary.
static std::string describe_model_settings(const fin::ml::IModel &model)
{
    std::ostringstream out;
    out << "tf " << (model.timeframe().empty() ? "unrecorded" : model.timeframe());
    if (model.training_params().empty())
        out << ", periods unrecorded";
    for (const auto &[key, value] : model.training_params())
        out << ", " << key << '=' << value;
    return out.str();
}

// --model-weight, refused unless finite and >= 0 (run_scenario applies the same rule).
static bool parse_model_weight(const std::vector<std::string> &args, double &weight)
{
    if (auto v = parse_double_flag(args, "--model-weight"))
    {
        if (!std::isfinite(*v) || *v < 0.0)
        {
            std::cerr << "Invalid --model-weight (must be a finite number >= 0): " << *v << "\n";
            return false;
        }
        weight = *v;
    }
    return true;
}

static int cmd_backtest(const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cerr << "Usage: aiquant backtest <ticks.csv> [--tf S1|S5|M1|M5|H1] [--cash N] [--qty N] [--fee N] [--ema-fast N] [--ema-slow N] [--rsi N] [--macd-fast N] [--macd-slow N] [--macd-signal N] [--rsi-buy N] [--rsi-sell N] [--no-ema-xover] [--model-weight W] [--candles-out path] [--model-linear path]\n";
        return 2;
    }

    const std::string path = args[0];

    std::optional<fin::io::Timeframe> typed_tf;
    if (!explicit_timeframe(args, typed_tf))
        return 2;

    // The model comes first: the candles, the features and their periods may all be the ones
    // it records, and they have to be settled before a single candle is built.
    std::shared_ptr<fin::ml::IModel> model_ptr;
    fin::stream::SymbolModel settings;
    if (auto model_path = parse_string_flag(args, "--model-linear"))
    {
        std::string error;
        auto loaded = fin::ml::load_model_file(*model_path, error);
        if (!loaded)
        {
            // It used to carry on without the model, which reported a backtest of a strategy
            // nobody asked for.
            std::cerr << error << "\n";
            return 1;
        }
        if (!fin::stream::symbol_model_from(loaded, explicit_overrides(args, {}, typed_tf), {}, settings, error))
        {
            std::cerr << error << "\n";
            return 1;
        }
        model_ptr = loaded;
        std::cerr << "Model: " << describe_model_settings(*loaded) << "\n";
    }

    fin::io::TickCsvOptions opt{}; // defaults: header, epoch-ms
    const auto tf = settings.timeframe.value_or(typed_tf.value_or(fin::io::Timeframe::M1));
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

    // With a model, the features are the ones it was trained on, computed with its periods --
    // this used to build the default six with the flags' periods whatever the file said. The
    // snapshot EMA/RSI take the same periods, as they do in run_scenario.
    std::unique_ptr<fin::indicators::FeatureBus> feature_bus;
    if (model_ptr)
    {
        const auto &params = *settings.params;
        cfg.ema_fast = params.ema_fast;
        cfg.ema_slow = params.ema_slow;
        cfg.rsi_period = params.rsi;
        const auto &features = settings.features.empty() ? fin::indicators::default_feature_names() : settings.features;
        feature_bus = std::make_unique<fin::indicators::FeatureBus>(features, params);
    }
    // Signal config (MVP): RSI Thresholds and EMA crossover on/off
    fin::signal::SignalEngineConfig scfg{}; // defaults: buy <= 30, sell >= 70, use EMA crossover
    // Both spellings, as run-mvp takes them: the usage has always said --rsi-buy, and only
    // --rsi_buy was ever read.
    if (auto v = parse_double_flag(args, "--rsi-buy"))
        scfg.rsi_buy_below = *v;
    else if (auto v2 = parse_double_flag(args, "--rsi_buy"))
        scfg.rsi_buy_below = *v2;
    if (auto v = parse_double_flag(args, "--rsi-sell"))
        scfg.rsi_sell_above = *v;
    if (flag_present(args, "--no-ema-xover"))
        scfg.use_ema_crossover = false;
    if (!parse_model_weight(args, scfg.model_weight))
        return 2;

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
                    if (model_ptr)
                        prediction = model_ptr->predict(fv);
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
    if (model_ptr)
        std::cout << "Signals decided by the model: " << m.model_decisive_signals
                  << " (model weight " << scfg.model_weight << ")\n";

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

    // Record what the features were computed with, as run_scenario does, so a reader can
    // rebuild them. FeatureBus's positional constructor always builds the default six.
    fin::indicators::FeatureParams trained_with{};
    trained_with.ema_fast = ema_fast;
    trained_with.rsi = rsi_period;
    trained_with.macd_fast = macd_fast;
    trained_with.macd_slow = macd_slow;
    trained_with.macd_signal = macd_signal;
    summary.model.set_training_params(
        fin::indicators::feature_params_for(fin::indicators::default_feature_names(), trained_with));
    summary.model.set_timeframe(fin::io::timeframe_token(tf));

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

// Writes the human-readable report to `os`. With --json the report goes to stderr so that
// stdout carries nothing but the JSON document and stays pipeable into jq.
static void print_scenario_result(const fin::app::ScenarioConfig &cfg, const fin::app::ScenarioResult &result,
                                  std::ostream &os)
{
    os << "=== MVP scenario ===\n";
    os << "Ticks: " << cfg.ticks_path << "\n";
    if (!result.symbol.empty())
    {
        os << "Symbol: " << result.symbol;
        if (result.ticks_other_symbol > 0)
            os << " (" << result.ticks_other_symbol << " ticks for other symbols skipped)";
        os << "\n";
    }
    os << "Timeframe: " << fin::io::timeframe_token(cfg.timeframe) << "\n";
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

    // Out of sample first: it is the figure that means something. The training stretch
    // follows for comparison, labelled so it is never read as the result.
    const auto print_metrics = [&os](const fin::backtest::Metrics &m)
    {
        os << "  Final cash: " << m.final_cash << "\n";
        os << "  PnL: " << m.pnl << " (" << m.return_pct << "%)\n";
        os << "  Trades: " << m.trades << " (Wins: " << m.wins << ", Losses: " << m.losses << ")\n";
        os << "  Signals decided by the model: " << m.model_decisive_signals << "\n";
        os << "  Max DD: " << m.max_drawdown << "%\n";
    };
    os << "Backtest, out of sample (" << result.out_of_sample_candles << " candles from "
       << result.out_of_sample_from_ms << ", model weight " << cfg.model_weight << "):\n";
    print_metrics(result.metrics);
    os << "Backtest, in sample (" << result.in_sample_candles
       << " candles up to the training split, warmup included; for comparison only):\n";
    print_metrics(result.metrics_in_sample);
    if (result.model_saved && cfg.model_output_path)
        os << "Saved model: " << *cfg.model_output_path << "\n";
}

static int cmd_run_mvp(const std::vector<std::string> &args)
{
    if (args.empty())
    {
        std::cerr << "Usage: aiquant run-mvp <ticks.csv> [--symbol SYM] [--tf S1|S5|M1|M5|H1] [--train-ratio 0.1-0.95] [--ridge L] [--model ridge|sgd|mlp] [--sgd-lr N] [--sgd-l2 N] [--sgd-epochs N] [--sgd-power-t N] [--no-sgd-standardize] [--online] [--mlp-hidden 16,8] [--mlp-lr N] [--mlp-l2 N] [--mlp-epochs N] [--mlp-activation tanh|relu] [--mlp-seed N] [--cash N] [--qty N] [--fee N] [--ema-fast N] [--ema-slow N] [--rsi N] [--macd-fast N] [--macd-slow N] [--macd-signal N] [--rsi-buy N|--rsi_buy N] [--rsi-sell N|--rsi_sell N] [--no-ema-xover] [--model-weight W] [--preview N] [--preview-out path] [--model-out path] [--features a,b,c] [--json]\n";
        return 2;
    }

    fin::app::ScenarioConfig cfg{};
    cfg.ticks_path = args[0];
    cfg.timeframe = parse_timeframe_flag(args);

    // Empty binds to the first tick's symbol, so a single-symbol file needs nothing here.
    if (auto symbol = parse_string_flag(args, "--symbol"))
        cfg.symbol = *symbol;

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
        else if (token == "mlp")
            cfg.model = fin::app::ModelKind::Mlp;
        else if (token == "ridge" || token == "linear")
            cfg.model = fin::app::ModelKind::Ridge;
        else
        {
            std::cerr << "Unknown --model '" << *model << "' (expected ridge, sgd or mlp)\n";
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

    if (auto hidden = parse_string_flag(args, "--mlp-hidden"))
    {
        // Comma-separated hidden-layer widths, e.g. --mlp-hidden 16,8.
        std::vector<std::size_t> widths;
        std::size_t start = 0;
        const std::string &list = *hidden;
        bool ok = true;
        while (start <= list.size())
        {
            const std::size_t comma = list.find(',', start);
            const std::size_t end = (comma == std::string::npos) ? list.size() : comma;
            std::string item = list.substr(start, end - start);
            const auto first = item.find_first_not_of(" \t");
            const auto last = item.find_last_not_of(" \t");
            if (first != std::string::npos)
            {
                item = item.substr(first, last - first + 1);
                std::size_t w = 0;
                auto [ptr, ec] = std::from_chars(item.data(), item.data() + item.size(), w);
                if (ec != std::errc{} || ptr != item.data() + item.size())
                    ok = false;
                else
                    widths.push_back(w);
            }
            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }
        if (!ok || widths.empty())
        {
            std::cerr << "Invalid --mlp-hidden '" << *hidden << "' (expected comma-separated widths, e.g. 16,8)\n";
            return 2;
        }
        cfg.mlp.hidden_layers = std::move(widths);
    }
    if (auto v = parse_double_flag(args, "--mlp-lr"))
        cfg.mlp.learning_rate = *v;
    if (auto v = parse_double_flag(args, "--mlp-l2"))
        cfg.mlp.l2 = *v;
    if (auto v = parse_size_flag(args, "--mlp-epochs"))
        cfg.mlp.epochs = *v;
    if (auto v = parse_size_flag(args, "--mlp-seed"))
        cfg.mlp.seed = static_cast<std::uint64_t>(*v);
    if (auto act = parse_string_flag(args, "--mlp-activation"))
    {
        std::string token = *act;
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
        if (token == "tanh")
            cfg.mlp.activation = fin::ml::MlpActivation::Tanh;
        else if (token == "relu")
            cfg.mlp.activation = fin::ml::MlpActivation::Relu;
        else
        {
            std::cerr << "Unknown --mlp-activation '" << *act << "' (expected tanh or relu)\n";
            return 2;
        }
    }

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
    if (auto v = parse_double_flag(args, "--model-weight"))
        cfg.model_weight = *v; // run_scenario validates it

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
        std::cerr << "Usage: aiquant stream <ticks.csv> [--tf S1|S5|M1|M5|H1] [--model-linear path] [--features a,b,c] [--symbol SYM | --per-symbol [--model-dir DIR]] [--ema-fast N] [--ema-slow N] [--rsi N] [--macd-fast N] [--macd-slow N] [--macd-signal N] [--rsi-buy N] [--rsi-sell N] [--no-ema-xover] [--model-weight W] [--all] [--limit N]\n";
        return 2;
    }

    const std::string path = args[0];

    fin::stream::StreamConfig cfg{};
    std::optional<fin::io::Timeframe> typed_tf;
    if (!explicit_timeframe(args, typed_tf))
        return 2;
    cfg.timeframe = typed_tf.value_or(fin::io::Timeframe::M1);

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
    if (!parse_model_weight(args, cfg.signal.model_weight))
        return 2;

    const bool per_symbol = flag_present(args, "--per-symbol");
    if (auto symbol = parse_string_flag(args, "--symbol"))
    {
        if (per_symbol)
        {
            std::cerr << "--symbol and --per-symbol ask for one instrument and for all of them; pick one\n";
            return 2;
        }
        // Naming a symbol is how you say "this file holds several; take mine and skip the
        // rest". Without it, a second symbol is an error rather than a silent blend.
        cfg.symbol = *symbol;
        cfg.foreign_symbol = fin::stream::SymbolPolicy::Skip;
    }
    else if (per_symbol)
    {
        // Every instrument in the file gets its own candles, indicators and signals, all
        // judged by the one model. The CSV rows carry the symbol, so they stay attributable.
        cfg.foreign_symbol = fin::stream::SymbolPolicy::Route;
    }

    cfg.features = parse_feature_list(args);

    const auto model_dir = parse_string_flag(args, "--model-dir");
    if (model_dir && !per_symbol)
    {
        std::cerr << "--model-dir holds one model per symbol and needs --per-symbol\n";
        return 2;
    }
    if (model_dir && parse_string_flag(args, "--model-linear"))
    {
        std::cerr << "--model-dir and --model-linear both choose the model; pick one\n";
        return 2;
    }

    // Each model file supplies the features, periods and timeframe it was trained with, and a
    // flag the user typed that contradicts one of them is refused here, before the first tick.
    const auto overrides = explicit_overrides(args, cfg.features, typed_tf);

    // One model per symbol, all loaded before the first tick: a bad file fails here rather
    // than mid-feed, and no path is ever built from a symbol the feed supplied.
    std::unordered_map<std::string, fin::stream::SymbolModel> resolved_models;
    std::size_t models_loaded = 0;
    if (model_dir)
    {
        std::unordered_map<std::string, std::shared_ptr<fin::ml::IModel>> models;
        std::string error;
        if (!fin::ml::load_model_dir(*model_dir, models, error))
        {
            std::cerr << error << "\n";
            return 1;
        }
        models_loaded = models.size();
        for (auto &[symbol, loaded] : models)
        {
            fin::stream::SymbolModel resolved;
            if (!fin::stream::symbol_model_from(loaded, overrides, cfg.params, resolved, error))
            {
                std::cerr << "model for " << symbol << ": " << error << "\n";
                return 1;
            }
            resolved_models.emplace(symbol, std::move(resolved));
        }
    }

    // The single --model-linear model, resolved the same way. Without one, the stream runs on
    // the flags alone, as it always has.
    fin::stream::SymbolModel single{};
    if (auto model_path = parse_string_flag(args, "--model-linear"))
    {
        std::string error;
        auto loaded = fin::ml::load_model_file(*model_path, error);
        if (!loaded)
        {
            std::cerr << error << "\n";
            return 1;
        }
        if (!fin::stream::symbol_model_from(loaded, overrides, cfg.params, single, error))
        {
            std::cerr << error << "\n";
            return 1;
        }
        std::cerr << "Model: " << describe_model_settings(*loaded) << "\n";
    }

    CsvSignalSink sink(flag_present(args, "--all"), parse_size_flag(args, "--limit").value_or(0));
    // Without --model-dir every symbol gets the one --model-linear model, as before.
    fin::stream::ModelResolver resolver{[&](const std::string &symbol)
                                        {
                                            if (!model_dir)
                                                return single;
                                            const auto it = resolved_models.find(symbol);
                                            if (it == resolved_models.end())
                                                return fin::stream::SymbolModel{}; // no predictions
                                            return it->second;
                                        }};
    fin::stream::StreamEngine engine(cfg, std::move(resolver), &sink);

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
    if (per_symbol)
    {
        const auto by_symbol = engine.stats_by_symbol();
        std::cerr << "Symbols: " << by_symbol.size() << "\n";
        if (model_dir)
            std::cerr << "Models: " << models_loaded << " loaded from " << *model_dir << "\n";
        for (const auto &[symbol, s] : by_symbol)
        {
            std::cerr << "  " << symbol << ": " << s.ticks << " ticks, " << s.candles
                      << " candles, " << s.feature_rows << " feature rows, signals Buy " << s.buys
                      << " / Sell " << s.sells << " / Hold " << s.holds;
            if (!engine.has_model(symbol))
                std::cerr << " (no model)";
            std::cerr << "\n";
        }
    }
    else
    {
        std::cerr << "Symbol: " << engine.bound_symbol() << "\n";
    }
    std::cerr << "Candles: " << stats.candles << ", feature rows: " << stats.feature_rows
              << ", predictions: " << stats.predictions;
    if (stats.prediction_errors > 0)
        std::cerr << " (" << stats.prediction_errors << " failed)";
    std::cerr << "\n";
    std::cerr << "Signals: " << stats.signals << " (Buy " << stats.buys << ", Sell " << stats.sells
              << ", Hold " << stats.holds << ") - printed " << sink.printed() << "\n";
    std::cerr << "Signals decided by the model: " << stats.model_decisive << " (model weight "
              << cfg.signal.model_weight << ")\n";
    return 0;
}

int main(int argc, char **argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty())
    {
        std::cout << "AiQuant CLI (MVP)\n";
        std::cout << "Commands: \n";
        std::cout << "  backtest <ticks.csv> [--tf S1|S5|M1|M5|H1] [--cash N] [--qty N] [--fee N] [--ema-fast N] [--ema-slow N] [--rsi N] [--macd-fast N] [--macd-slow N] [--macd-signal N] [--rsi-buy N] [--rsi-sell N] [--no-ema-xover] [--model-weight W] [--candles-out path] [--model-linear path]\n";
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
