#pragma once
#ifndef FIN_BACKTEST_BACKTESTER_HPP
#define FIN_BACKTEST_BACKTESTER_HPP

#include <vector>
#include <optional>
#include <string>

#include "fin/core/Candle.hpp"
#include "fin/indicators/adapters/CandleAdapters.hpp" // EMAFromCandle, RSIFromCandle
#include "fin/signal/SignalEngine.hpp"

namespace fin::backtest
{
    struct Trade
    {
        fin::core::Timestamp entry_ts{};
        fin::core::Timestamp exit_ts{};
        double entry_price = 0.0;
        double exit_price = 0.0;
        double qty = 0.0;
        double pnl = 0.0;
    };

    struct BacktestConfig
    {
        double initial_cash = 10'000.0;
        double trade_qty = 1.0;           // units per trade
        double fee_per_trade = 0.0;       // fixed fee per transaction
        std::size_t ema_fast = 12;
        std::size_t ema_slow = 26;
        std::size_t rsi_period = 14;
    };

    struct Metrics
    {
        double final_cash = 0.0;
        double pnl = 0.0;
        double return_pct = 0.0;     // percent
        double max_drawdown = 0.0;   // percent (peak-to-trough)
        int trades = 0;
        int wins = 0;
        int losses = 0;
    };

    class Backtester
    {
    public:
        Backtester(BacktestConfig cfg = {}, fin::signal::SignalEngine engine = fin::signal::SignalEngine(fin::signal::SignalEngineConfig{}));

        void reset();

        // Feed one candle; applies strategy and updates positions
        void on_candle(const fin::core::Candle &c, std::optional<double> prediction = std::nullopt);

        // Close any open position at last price and compute metrics
        Metrics finalize();

        const std::vector<Trade> &trades() const { return trades_; }

    private:
        BacktestConfig cfg_{};
        fin::signal::SignalEngine engine_{};

        // State
        double cash_ = 0.0;
        double qty_ = 0.0; // position size (long-only)
        double last_close_ = 0.0;
        fin::core::Timestamp last_ts_{};
        double entry_price_ = 0.0;
        fin::core::Timestamp entry_ts_{};

        double equity_peak_ = 0.0;
        double max_drawdown_pct_ = 0.0; // peak-to-trough in percent
        std::vector<Trade> trades_;

        // Indicators (from candles)
        fin::indicators::EMAFromCandle ema_fast_;
        fin::indicators::EMAFromCandle ema_slow_;
        fin::indicators::RSIFromCandle rsi_;

        fin::signal::IndicatorsSnapshot make_snapshot(const fin::core::Candle &c) const;
        void apply_signal(const fin::core::Candle &c, const fin::signal::Signal &sig);
        void update_drawdown(const fin::core::Candle &c);
    };

} // namespace fin::backtest

#endif /*FIN_BACKTEST_BACKTESTER_HPP*/
