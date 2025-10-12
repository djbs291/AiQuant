#pragma once

#include <optional>
#include <string>
#include <vector>

#include "fin/io/Pipeline.hpp"
#include "fin/ml/LinearTrainer.hpp"
#include "fin/backtest/Backtester.hpp"

namespace fin::app
{
    struct ScenarioConfig
    {
        std::string ticks_path;
        fin::io::Timeframe timeframe = fin::io::Timeframe::M1;
        double train_ratio = 0.7;
        double ridge_lambda = 1e-6;

        std::size_t ema_fast = 12;
        std::size_t ema_slow = 26;
        std::size_t rsi_period = 14;
        std::size_t macd_fast = 12;
        std::size_t macd_slow = 26;
        std::size_t macd_signal = 9;

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

        std::size_t validation_samples = 0;
        double validation_rmse = 0.0;
        std::vector<ScenarioPreview> validation_preview;

        fin::ml::LinearTrainingSummary training;
        fin::backtest::Metrics metrics;
        bool model_saved = false;
    };

    ScenarioResult run_scenario(const ScenarioConfig &config);
}
