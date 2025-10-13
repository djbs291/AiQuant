#include "fin/app/ScenarioRunner.hpp"

#include <chrono>
#include <cmath>
#include <stdexcept>

#include "fin/indicators/FeatureBus.hpp"
#include "fin/ml/FeatureVector.hpp"
#include "fin/signal/SignalEngine.hpp"

namespace fin::app
{
    namespace
    {
        std::size_t clamp_training_rows(std::size_t total_rows, double ratio)
        {
            if (total_rows < 3)
                throw std::runtime_error("Need at least three feature rows to run scenario");

            if (ratio < 0.1)
                ratio = 0.1;
            else if (ratio > 0.95)
                ratio = 0.95;

            std::size_t rows = static_cast<std::size_t>(ratio * static_cast<double>(total_rows));
            if (rows < 2)
                rows = 2;
            if (rows >= total_rows)
                rows = total_rows - 1;
            return rows;
        }
    } // namespace

    ScenarioResult run_scenario(const ScenarioConfig &config)
    {
        if (config.ticks_path.empty())
            throw std::invalid_argument("ScenarioConfig.ticks_path is empty");

        fin::io::TickCsvOptions csv_opt{};
        auto res = fin::io::resample_csv_with_stats(config.ticks_path, config.timeframe, csv_opt);

        ScenarioResult result{};
        result.candles = res.candles.size();

        fin::indicators::FeatureBus feature_bus(config.ema_fast, config.rsi_period,
                                                config.macd_fast, config.macd_slow, config.macd_signal);
        std::vector<fin::indicators::FeatureRow> rows;
        rows.reserve(res.candles.size());
        for (const auto &c : res.candles)
        {
            if (auto row = feature_bus.update(c))
                rows.push_back(*row);
        }

        if (rows.size() < 3)
            throw std::runtime_error("Insufficient data after indicator warmup");

        result.feature_rows = rows.size();
        result.warmup_candles = result.candles - rows.size();

        std::size_t train_rows = clamp_training_rows(rows.size(), config.train_ratio);
        auto train_end = rows.begin() + static_cast<std::vector<fin::indicators::FeatureRow>::difference_type>(train_rows + 1);
        std::vector<fin::indicators::FeatureRow> training(rows.begin(), train_end);

        fin::ml::LinearTrainingOptions train_opts{};
        train_opts.ridge_lambda = config.ridge_lambda;

        fin::ml::LinearTrainingSummary training_summary = fin::ml::train_linear_from_feature_rows(training, train_opts);
        result.training = training_summary;

        double sse = 0.0;
        std::size_t validation_samples = 0;
        std::size_t preview_limit = config.validation_preview_limit;
        if (preview_limit == 0)
            preview_limit = 3;

        for (std::size_t i = train_rows; i + 1 < rows.size(); ++i)
        {
            auto fv = fin::ml::FeatureVector::from_feature_row(rows[i]);
            const double pred = training_summary.model.predict(fv);
            const double target = rows[i + 1].close - rows[i].close;
            const double err = pred - target;
            sse += err * err;
            ++validation_samples;

            if (result.validation_preview.size() < preview_limit)
            {
                using namespace std::chrono;
                const long long ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(rows[i + 1].ts.time_since_epoch()).count();
                result.validation_preview.push_back({ts_ms, pred, target});
            }
        }

        result.validation_samples = validation_samples;
        if (validation_samples > 0)
            result.validation_rmse = std::sqrt(sse / static_cast<double>(validation_samples));

        if (config.model_output_path)
        {
            if (!fin::ml::save_linear_model(training_summary.model, *config.model_output_path))
                throw std::runtime_error("Failed to persist linear model to " + *config.model_output_path);
            result.model_saved = true;
        }

        fin::signal::SignalEngineConfig scfg{};
        scfg.rsi_buy_below = config.rsi_buy;
        scfg.rsi_sell_above = config.rsi_sell;
        scfg.use_ema_crossover = config.use_ema_crossover;

        fin::signal::SignalEngine engine{scfg};

        fin::backtest::BacktestConfig btcfg{};
        if (config.initial_cash)
            btcfg.initial_cash = *config.initial_cash;
        if (config.trade_qty)
            btcfg.trade_qty = *config.trade_qty;
        if (config.fee_per_trade)
            btcfg.fee_per_trade = *config.fee_per_trade;
        btcfg.ema_fast = config.ema_fast;
        btcfg.ema_slow = config.ema_slow;
        btcfg.rsi_period = config.rsi_period;

        fin::backtest::Backtester bt(btcfg, engine);

        fin::indicators::FeatureBus live_bus(config.ema_fast, config.rsi_period,
                                             config.macd_fast, config.macd_slow, config.macd_signal);
        std::optional<double> pending_prediction;

        for (const auto &c : res.candles)
        {
            bt.on_candle(c, pending_prediction);

            if (auto row = live_bus.update(c))
            {
                auto fv = fin::ml::FeatureVector::from_feature_row(*row);
                pending_prediction = training_summary.model.predict(fv);
            }
            else
            {
                pending_prediction.reset();
            }
        }

        result.metrics = bt.finalize();
        return result;
    }
}
