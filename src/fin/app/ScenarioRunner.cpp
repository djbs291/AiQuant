#include "fin/app/ScenarioRunner.hpp"

#include <chrono>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "fin/app/ScenarioUtils.hpp"
#include "fin/indicators/FeatureBus.hpp"
#include "fin/indicators/FeatureSpec.hpp"
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

    bool validate_scenario_config(const ScenarioConfig &config, std::string &error)
    {
        // A period of zero does not fail loudly: the indicator simply never becomes ready,
        // and the run dies much later with "Insufficient data after indicator warmup", which
        // blames the data for what is a configuration mistake.
        const std::pair<const char *, std::size_t> periods[] = {
            {"ema_fast", config.ema_fast},
            {"ema_slow", config.ema_slow},
            {"rsi", config.rsi_period},
            {"macd_fast", config.macd_fast},
            {"macd_slow", config.macd_slow},
            {"macd_signal", config.macd_signal},
            {"sma", config.sma_period},
            {"bb_period", config.bb_period},
            {"atr", config.atr_period},
            {"adx", config.adx_period},
            {"stoch_k", config.stoch_k_period},
            {"stoch_d", config.stoch_d_period},
            {"zscore", config.zscore_period},
            {"momentum", config.momentum_period},
            {"sgd_epochs", config.sgd.epochs},
        };
        for (const auto &[name, value] : periods)
        {
            if (value == 0)
            {
                error = std::string("Invalid ") + name + ": must be >= 1";
                return false;
            }
        }

        // Every range check below is a comparison, and NaN passes a comparison by being false
        // both ways, so finiteness comes first. train_ratio has no range to check (the runner
        // clamps it), but a NaN one reaches clamp_training_rows and a NaN -> size_t cast,
        // which is undefined behaviour.
        const std::pair<const char *, double> numbers[] = {
            {"train_ratio", config.train_ratio},
            {"ridge", config.ridge_lambda},
            {"bb_k", config.bb_k},
            {"rsi_buy", config.rsi_buy},
            {"rsi_sell", config.rsi_sell},
            {"model_weight", config.model_weight},
            {"cash", config.initial_cash.value_or(1.0)},
            {"qty", config.trade_qty.value_or(1.0)},
            {"fee", config.fee_per_trade.value_or(0.0)},
            {"sgd_learning_rate", config.sgd.learning_rate},
            {"sgd_l2", config.sgd.l2},
            {"sgd_power_t", config.sgd.power_t},
            {"mlp_learning_rate", config.mlp.learning_rate},
            {"mlp_l2", config.mlp.l2},
        };
        for (const auto &[name, value] : numbers)
        {
            if (!std::isfinite(value))
            {
                error = std::string("Invalid ") + name + ": must be a finite number";
                return false;
            }
        }

        if (config.bb_k <= 0.0)
        {
            error = "Invalid bb_k: the band width must be > 0";
            return false;
        }

        // A negative ridge term is not regularization, it is anti-regularization: it quietly
        // made the fitted model about thirty times worse on the sample scenario.
        if (config.ridge_lambda < 0.0)
        {
            error = "Invalid ridge: the regularization term must be >= 0";
            return false;
        }

        const std::pair<const char *, double> thresholds[] = {
            {"rsi_buy", config.rsi_buy},
            {"rsi_sell", config.rsi_sell},
        };
        for (const auto &[name, value] : thresholds)
        {
            if (value < 0.0 || value > 100.0)
            {
                error = std::string("Invalid ") + name + ": an RSI threshold lies in [0, 100]";
                return false;
            }
        }

        // A negative weight would turn the model into its own contrarian; zero leaves it out.
        if (config.model_weight < 0.0)
        {
            error = "Invalid model_weight: must be >= 0";
            return false;
        }

        // The backtester trusts all three. A negative quantity makes a Buy pay out, so on the
        // MVP scenario `qty = -5` reported +844% without a single trade; a negative fee turned
        // every one of 18 trades into a win.
        if (config.initial_cash && *config.initial_cash <= 0.0)
        {
            error = "Invalid cash: the starting cash must be > 0";
            return false;
        }
        if (config.trade_qty && *config.trade_qty <= 0.0)
        {
            error = "Invalid qty: the quantity per trade must be > 0";
            return false;
        }
        if (config.fee_per_trade && *config.fee_per_trade < 0.0)
        {
            error = "Invalid fee: the fee per trade must be >= 0";
            return false;
        }

        // SgdRegressor refuses these too, but only when model = sgd; a scenario that carries
        // them while training ridge is still carrying a mistake.
        if (config.sgd.learning_rate <= 0.0)
        {
            error = "Invalid sgd_learning_rate: must be > 0";
            return false;
        }
        if (config.sgd.l2 < 0.0)
        {
            error = "Invalid sgd_l2: must be >= 0";
            return false;
        }
        if (config.sgd.power_t < 0.0)
        {
            error = "Invalid sgd_power_t: must be >= 0";
            return false;
        }

        if (config.online_update && config.model != ModelKind::Sgd)
        {
            // Accepting it silently would report an online run whose model never moved.
            error = "online_update requires model = sgd: the ridge model is a closed-form fit "
                    "with no partial_fit to call";
            return false;
        }

        // MlpRegressor refuses these too, but only when model = mlp; a scenario that carries a
        // broken architecture while training something else is still carrying a mistake.
        if (config.mlp.hidden_layers.empty())
        {
            error = "Invalid mlp_hidden: an MLP needs at least one hidden layer";
            return false;
        }
        for (std::size_t width : config.mlp.hidden_layers)
        {
            if (width == 0)
            {
                error = "Invalid mlp_hidden: every hidden layer must have width >= 1";
                return false;
            }
        }
        if (config.mlp.learning_rate <= 0.0)
        {
            error = "Invalid mlp_learning_rate: must be > 0";
            return false;
        }
        if (config.mlp.l2 < 0.0)
        {
            error = "Invalid mlp_l2: must be >= 0";
            return false;
        }
        if (config.mlp.epochs == 0)
        {
            error = "Invalid mlp_epochs: must be >= 1";
            return false;
        }

        return true;
    }

    ScenarioResult run_scenario(const ScenarioConfig &config)
    {
        if (config.ticks_path.empty())
            throw std::invalid_argument("ScenarioConfig.ticks_path is empty");

        if (std::string error; !validate_scenario_config(config, error))
            throw std::invalid_argument(error);

        fin::io::TickCsvOptions csv_opt{};
        auto res = fin::io::resample_csv_with_stats(config.ticks_path, config.timeframe, csv_opt,
                                                   config.symbol);

        // A file-level problem — unopenable, or a header missing a column every row needs —
        // would otherwise surface further down as "0 candles produced 0 feature rows", which
        // says nothing about the cause. Report what the reader actually found.
        if (!res.error.empty())
            throw std::runtime_error(res.error);

        ScenarioResult result{};
        result.candles = res.candles.size();
        result.symbol = res.symbol;
        result.ticks_other_symbol = res.ticks_other_symbol;
        result.model = (config.model == ModelKind::Sgd) ? "sgd"
                       : (config.model == ModelKind::Mlp) ? "mlp"
                                                          : "ridge";
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
        // The MLP does not fold into linear weights, so it is not exported: it makes its own
        // predictions in the validation scoring and the backtest replay below.
        std::optional<fin::ml::MlpRegressor> mlp_model;

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
        else if (config.model == ModelKind::Mlp)
        {
            fin::ml::MlpTrainingSummary mlp_summary;
            try
            {
                mlp_summary = fin::ml::train_mlp_from_feature_rows(training, config.mlp);
            }
            catch (const std::exception &ex)
            {
                // Divergence names the learning rate itself; add what it was fitting.
                throw std::runtime_error(std::string(ex.what()) + " (mlp over " +
                                         std::to_string(feature_names.size()) + " features: [" +
                                         join_names(feature_names) + "])");
            }

            // No linear export: training_summary.model stays empty, so the report prints no
            // weights for it. The fit quality still travels, beside the other two models'.
            training_summary.mse = mlp_summary.mse;
            training_summary.samples = mlp_summary.samples;
            mlp_model = std::move(mlp_summary.model);
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
        // Carried by the model, and so written into any file it is saved to: a per-symbol model
        // directory can then tell a model stored under the wrong symbol's name. The periods and
        // candles its features were computed with travel too, so whoever loads the file rebuilds
        // the same features rather than whatever its own flags say.
        const auto saved_params =
            fin::indicators::feature_params_for(result.features, make_feature_params(config));
        const std::string saved_tf = fin::io::timeframe_token(config.timeframe);
        if (mlp_model)
        {
            // The MLP carries its own metadata, since it is saved and served as itself.
            mlp_model->set_symbol(result.symbol);
            mlp_model->set_training_params(saved_params);
            mlp_model->set_timeframe(saved_tf);
        }
        else
        {
            training_summary.model.set_symbol(result.symbol);
            training_summary.model.set_training_params(saved_params);
            training_summary.model.set_timeframe(saved_tf);
        }
        result.training = training_summary;

        // The model that makes the frozen (as-trained) predictions, in the validation scoring
        // and in the backtest replay when nothing is learning online. For ridge and sgd it is
        // the exported LinearModel; the MLP predicts directly, since it has no linear form.
        const fin::ml::IModel &frozen_model =
            mlp_model ? static_cast<const fin::ml::IModel &>(*mlp_model)
                      : static_cast<const fin::ml::IModel &>(training_summary.model);

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
            const double pred = frozen_model.predict(fv);
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
            // training run, not of the replay that follows it. An MLP is written in its own
            // format (it does not fold into linear weights); ridge and sgd share the linear one.
            const bool saved = mlp_model
                                   ? fin::ml::save_mlp_model(*mlp_model, *config.model_output_path)
                                   : fin::ml::save_linear_model(training_summary.model, *config.model_output_path);
            if (!saved)
                throw std::runtime_error("Failed to persist model to " + *config.model_output_path);
            result.model_saved = true;
        }

        fin::signal::SignalEngineConfig scfg{};
        scfg.rsi_buy_below = config.rsi_buy;
        scfg.rsi_sell_above = config.rsi_sell;
        scfg.use_ema_crossover = config.use_ema_crossover;
        scfg.model_weight = config.model_weight;

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

        // Two backtesters over one replay. Until 2026-09 there was one, over every candle, so
        // about train_ratio of each reported trade, PnL and drawdown came from candles the model
        // had been fitted to. `in_sample` trades up to the split; `out_of_sample` only watches
        // until then, so its indicators are warm, and trades from the first validation row on.
        fin::backtest::Backtester in_sample(btcfg, engine);
        fin::backtest::Backtester out_of_sample(btcfg, engine);
        bool past_split = false;

        // Same feature set as training: if these two ever diverged, the backtest would feed the
        // model something it was not trained on.
        fin::indicators::FeatureBus live_bus(feature_names, feature_params);

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
            // Each candle is judged with the prediction made from its own features, at its own
            // close: the forecast of the move to the next close, which is what the model was
            // trained on and when the backtester trades. The indicator rules are judged on the
            // same candle. Until 2026-09 the prediction was held back one candle, so the model
            // voted on a move that had already happened.
            std::optional<double> prediction;
            if (auto row = live_bus.update(c))
            {
                // The first validation row -- the first one the model was not trained on --
                // opens the out-of-sample stretch, and every candle after it belongs there.
                if (emitted_rows >= train_rows && !past_split)
                {
                    past_split = true;
                    result.out_of_sample_from_ms =
                        std::chrono::duration_cast<std::chrono::milliseconds>(c.start_time().time_since_epoch()).count();
                }

                // emitted_rows > train_rows means the previous row is past the training split.
                // Re-learning the training rows here would only be extra passes over data the
                // model has already fitted. The previous row's target is known now, at this
                // close, so learning from it before predicting this row uses nothing ahead.
                if (live_model && previous_row && emitted_rows > train_rows)
                {
                    auto prev_fv = fin::ml::FeatureVector::from_feature_row(*previous_row);
                    live_model->partial_fit(prev_fv, row->close - previous_row->close);
                    ++result.online_updates;
                }

                auto fv = fin::ml::FeatureVector::from_feature_row(*row);
                prediction = live_model ? live_model->predict(fv) : frozen_model.predict(fv);
                previous_row = *row;
                ++emitted_rows;
            }

            if (past_split)
            {
                out_of_sample.on_candle(c, prediction);
                ++result.out_of_sample_candles;
            }
            else
            {
                in_sample.on_candle(c, prediction);
                out_of_sample.observe(c);
                ++result.in_sample_candles;
            }
        }

        result.metrics = out_of_sample.finalize();
        result.metrics_in_sample = in_sample.finalize();
        return result;
    }
}
