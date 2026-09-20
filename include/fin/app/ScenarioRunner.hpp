#pragma once

#include <optional>
#include <string>
#include <vector>

#include "fin/io/Pipeline.hpp"
#include "fin/ml/LinearTrainer.hpp"
#include "fin/ml/SgdRegressor.hpp"
#include "fin/backtest/Backtester.hpp"

namespace fin::app
{
    // Which trainer produces the model. Ridge is the closed-form solver the engine has always
    // used; Sgd is the online learner, and the only one `online_update` can drive.
    enum class ModelKind
    {
        Ridge,
        Sgd
    };

    struct ScenarioConfig
    {
        std::string ticks_path;
        // Which instrument to take out of the tick file. Empty binds to the first tick's
        // symbol, so a single-symbol file needs no key; naming one selects it out of a file
        // that holds several, instead of blending them into one candle series.
        std::string symbol;
        fin::io::Timeframe timeframe = fin::io::Timeframe::M1;
        double train_ratio = 0.7;
        double ridge_lambda = 1e-6;

        ModelKind model = ModelKind::Ridge;
        // Only read when model is Sgd.
        fin::ml::SgdOptions sgd{};
        // Keep learning through the out-of-sample stretch: every candle that closes hands the
        // model the target it has just realized, as one partial_fit. Requires model = Sgd.
        bool online_update = false;

        std::size_t ema_fast = 12;
        std::size_t ema_slow = 26;
        std::size_t rsi_period = 14;
        std::size_t macd_fast = 12;
        std::size_t macd_slow = 26;
        std::size_t macd_signal = 9;

        // Model feature set, by name. Empty means the historical six, so scenarios written
        // before this existed keep training exactly the same model.
        std::vector<std::string> features;

        // Periods for the indicators only the feature list can reach.
        std::size_t sma_period = 14;
        std::size_t bb_period = 20;
        double bb_k = 2.0;
        std::size_t atr_period = 14;
        std::size_t adx_period = 14;
        std::size_t stoch_k_period = 14;
        std::size_t stoch_d_period = 3;
        std::size_t zscore_period = 20;
        std::size_t momentum_period = 10;

        double rsi_buy = 30.0;
        double rsi_sell = 70.0;
        bool use_ema_crossover = true;

        std::optional<double> initial_cash;
        std::optional<double> trade_qty;
        std::optional<double> fee_per_trade;

        std::optional<std::string> model_output_path;
        std::size_t validation_preview_limit = 3;
    };

    struct ScenarioPreview
    {
        long long ts_ms = 0;
        double predicted_delta = 0.0;
        double actual_delta = 0.0;
    };

    struct ScenarioResult
    {
        std::size_t candles = 0;
        std::size_t warmup_candles = 0;
        std::size_t feature_rows = 0;

        // The symbol the run actually resolved to, and how many ticks belonged to some other
        // instrument and were left out. Both are reported in the JSON.
        std::string symbol;
        std::size_t ticks_other_symbol = 0;
        // The feature set actually used, resolved from the config (reported in the JSON).
        std::vector<std::string> features;

        // "ridge" or "sgd", resolved from the config and reported in the JSON.
        std::string model = "ridge";
        bool online_update = false;
        // partial_fit calls made during the backtest replay, all on out-of-sample candles.
        std::size_t online_updates = 0;
        // Prequential RMSE over the validation rows: score the sample, then learn from it.
        // Zero when online updating is off, where it would only repeat validation_rmse.
        double online_validation_rmse = 0.0;

        std::size_t validation_samples = 0;
        double validation_rmse = 0.0;
        std::vector<ScenarioPreview> validation_preview;

        // For an SGD run this is the exported equivalent (SgdRegressor::to_linear_model), so
        // the reported weights, the saved file and the served model are one and the same.
        fin::ml::LinearTrainingSummary training;
        fin::backtest::Metrics metrics;
        bool model_saved = false;
    };

    ScenarioResult run_scenario(const ScenarioConfig &config);
}
