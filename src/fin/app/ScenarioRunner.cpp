#include "fin/app/ScenarioRunner.hpp"

#include <chrono>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

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

    namespace
    {
        fin::indicators::FeatureParams make_feature_params(const ScenarioConfig &config)
        {
            fin::indicators::FeatureParams params{};
            params.sma = config.sma_period;
            params.ema_fast = config.ema_fast;
            params.ema_slow = config.ema_slow;
            params.rsi = config.rsi_period;
            params.macd_fast = config.macd_fast;
            params.macd_slow = config.macd_slow;
            params.macd_signal = config.macd_signal;
            params.bb_period = config.bb_period;
            params.bb_k = config.bb_k;
            params.atr = config.atr_period;
            params.adx = config.adx_period;
            params.stoch_k = config.stoch_k_period;
            params.stoch_d = config.stoch_d_period;
            params.zscore = config.zscore_period;
            params.momentum = config.momentum_period;
            return params;
        }

        std::string join_names(const std::vector<std::string> &names)
        {
            std::string out;
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                if (i > 0)
                    out += ", ";
                out += names[i];
            }
            return out;
        }
    }

    ScenarioResult run_scenario(const ScenarioConfig &config)
    {
        if (config.ticks_path.empty())
            throw std::invalid_argument("ScenarioConfig.ticks_path is empty");

        if (config.online_update && config.model != ModelKind::Sgd)
        {
            // Accepting it silently would report an online run whose model never moved.
            throw std::invalid_argument("online_update requires model = sgd: the ridge model is a "
                                        "closed-form fit with no partial_fit to call");
        }

        fin::io::TickCsvOptions csv_opt{};
        auto res = fin::io::resample_csv_with_stats(config.ticks_path, config.timeframe, csv_opt);

        // A file-level problem — unopenable, or a header missing a column every row needs —
        // would otherwise surface further down as "0 candles produced 0 feature rows", which
        // says nothing about the cause. Report what the reader actually found.
        if (!res.error.empty())
            throw std::runtime_error(res.error);

        ScenarioResult result{};
        result.candles = res.candles.size();
        result.model = (config.model == ModelKind::Sgd) ? "sgd" : "ridge";
        result.online_update = config.online_update;

        const std::vector<std::string> &feature_names =
            config.features.empty() ? fin::indicators::default_feature_names() : config.features;
        const fin::indicators::FeatureParams feature_params = make_feature_params(config);

        fin::indicators::FeatureBus feature_bus(feature_names, feature_params);
        result.features = feature_bus.schema().names;

        std::vector<fin::indicators::FeatureRow> rows;
        rows.reserve(res.candles.size());
        for (const auto &c : res.candles)
        {
            if (auto row = feature_bus.update(c))
                rows.push_back(*row);
        }

        if (rows.size() < 3)
        {
            // Naming the set matters now that it is configurable: a long-warmup feature such as
            // adx (2N-1 bars) can starve a file that was fine with the default six.
            throw std::runtime_error("Insufficient data after indicator warmup: " +
                                     std::to_string(res.candles.size()) + " candles produced only " +
                                     std::to_string(rows.size()) + " feature rows for features [" +
                                     join_names(feature_names) + "]");
        }

        result.feature_rows = rows.size();
        result.warmup_candles = result.candles - rows.size();

        std::size_t train_rows = clamp_training_rows(rows.size(), config.train_ratio);
        auto train_end = rows.begin() + static_cast<std::vector<fin::indicators::FeatureRow>::difference_type>(train_rows + 1);
        std::vector<fin::indicators::FeatureRow> training(rows.begin(), train_end);

        fin::ml::LinearTrainingSummary training_summary;
        // Kept beside the exported LinearModel because it is the only one that can go on
        // learning: everything downstream reads the export, the online phase reads this.
        std::optional<fin::ml::SgdRegressor> sgd_model;

        if (config.model == ModelKind::Sgd)
        {
            fin::ml::SgdTrainingSummary sgd_summary;
            try
            {
                sgd_summary = fin::ml::train_sgd_from_feature_rows(training, config.sgd);
            }
            catch (const std::exception &ex)
            {
                // Divergence names the learning rate itself; add what it was fitting.
                throw std::runtime_error(std::string(ex.what()) + " (sgd over " +
                                         std::to_string(feature_names.size()) + " features: [" +
                                         join_names(feature_names) + "])");
            }

            // The reported weights, --model-out and /predict all go through the folded
            // equivalent, so an SGD run is served exactly like a ridge one.
            training_summary.model = sgd_summary.model.to_linear_model();
            training_summary.mse = sgd_summary.mse;
            training_summary.samples = sgd_summary.samples;
            sgd_model = std::move(sgd_summary.model);
        }
        else
        {
            fin::ml::LinearTrainingOptions train_opts{};
            train_opts.ridge_lambda = config.ridge_lambda;

            try
            {
                training_summary = fin::ml::train_linear_from_feature_rows(training, train_opts);
            }
            catch (const std::runtime_error &ex)
            {
                // The solver refuses singular systems, which wide and near-collinear feature sets
                // make much easier to hit, so say what was being solved.
                throw std::runtime_error(std::string(ex.what()) + " (" + std::to_string(feature_names.size()) +
                                         " features: [" + join_names(feature_names) +
                                         "]; try a larger ridge or fewer correlated features)");
            }
        }
        result.training = training_summary;

        double sse = 0.0;
        std::size_t validation_samples = 0;
        std::size_t preview_limit = config.validation_preview_limit;
        if (preview_limit == 0)
            preview_limit = 3;

        // Prequential scoring on its own copy: predict the sample, then learn from it. The
        // backtest replay below starts again from the trained state, so the updates made here
        // must not be the ones it depends on.
        std::optional<fin::ml::SgdRegressor> prequential;
        if (config.online_update && sgd_model)
            prequential = *sgd_model;
        double online_sse = 0.0;

        for (std::size_t i = train_rows; i + 1 < rows.size(); ++i)
        {
            auto fv = fin::ml::FeatureVector::from_feature_row(rows[i]);
            const double pred = training_summary.model.predict(fv);
            const double target = rows[i + 1].close - rows[i].close;
            const double err = pred - target;
            sse += err * err;
            ++validation_samples;

            if (prequential)
            {
                const double online_err = prequential->predict(fv) - target;
                online_sse += online_err * online_err;
                prequential->partial_fit(fv, target);
            }

            if (result.validation_preview.size() < preview_limit)
            {
                using namespace std::chrono;
                const long long ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(rows[i + 1].ts.time_since_epoch()).count();
                result.validation_preview.push_back({ts_ms, pred, target});
            }
        }

        result.validation_samples = validation_samples;
        if (validation_samples > 0)
        {
            result.validation_rmse = std::sqrt(sse / static_cast<double>(validation_samples));
            if (prequential)
                result.online_validation_rmse = std::sqrt(online_sse / static_cast<double>(validation_samples));
        }

        if (config.model_output_path)
        {
            // The model as trained, before any online update: the file is the artifact of the
            // training run, not of the replay that follows it.
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

        // Same feature set as training: if these two ever diverged, the backtest would feed the
        // model something it was not trained on.
        fin::indicators::FeatureBus live_bus(feature_names, feature_params);
        std::optional<double> pending_prediction;

        // The deployed copy: it starts where training left off and learns only from candles
        // the training pass never saw.
        std::optional<fin::ml::SgdRegressor> live_model;
        if (config.online_update && sgd_model)
            live_model = *sgd_model;

        // A row's target is only realized when the next row closes, so the update always runs
        // one row behind. That is what keeps the replay free of lookahead.
        std::optional<fin::indicators::FeatureRow> previous_row;
        std::size_t emitted_rows = 0;

        for (const auto &c : res.candles)
        {
            bt.on_candle(c, pending_prediction);

            if (auto row = live_bus.update(c))
            {
                // emitted_rows > train_rows means the previous row is past the training split.
                // Re-learning the training rows here would only be extra passes over data the
                // model has already fitted.
                if (live_model && previous_row && emitted_rows > train_rows)
                {
                    auto prev_fv = fin::ml::FeatureVector::from_feature_row(*previous_row);
                    live_model->partial_fit(prev_fv, row->close - previous_row->close);
                    ++result.online_updates;
                }

                auto fv = fin::ml::FeatureVector::from_feature_row(*row);
                pending_prediction = live_model ? live_model->predict(fv)
                                                : training_summary.model.predict(fv);
                previous_row = *row;
                ++emitted_rows;
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
