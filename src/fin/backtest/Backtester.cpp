#include "fin/backtest/Backtester.hpp"

namespace fin::backtest
{
    using fin::core::Candle;
    using fin::signal::IndicatorsSnapshot;
    using fin::signal::Signal;
    using fin::signal::SignalType;

    Backtester::Backtester(BacktestConfig cfg, fin::signal::SignalEngine engine)
        : cfg_(cfg), engine_(engine),
          ema_fast_(cfg.ema_fast), ema_slow_(cfg.ema_slow), rsi_(cfg.rsi_period)
    {
        reset();
    }

    void Backtester::reset()
    {
        cash_ = cfg_.initial_cash;
        qty_ = 0.0;
        last_close_ = 0.0;
        entry_price_ = 0.0;
        trades_.clear();
        equity_peak_ = cfg_.initial_cash;
        max_drawdown_pct_ = 0.0;
        ema_fast_.reset();
        ema_slow_.reset();
        rsi_.reset();
    }

    IndicatorsSnapshot Backtester::make_snapshot(const Candle &c) const
    {
        IndicatorsSnapshot s{};
        s.ts = c.start_time();
        s.close = c.close().value();
        // symbol unknown at Candle level; leave empty
        return s;
    }

    void Backtester::apply_signal(const Candle &c, const Signal &sig)
    {
        const double px = c.close().value();
        last_close_ = px;
        last_ts_ = c.start_time();

        if (sig.type == SignalType::Buy && qty_ <= 0.0)
        {
            const double cost = px * cfg_.trade_qty + cfg_.fee_per_trade;
            if (cash_ >= cost)
            {
                cash_ -= cost;
                qty_ = cfg_.trade_qty;
                entry_price_ = px;
                entry_ts_ = c.start_time();
            }
        }
        else if (sig.type == SignalType::Sell && qty_ > 0.0)
        {
            const double proceeds = px * qty_ - cfg_.fee_per_trade;
            cash_ += proceeds;

            Trade t{};
            t.entry_ts = entry_ts_;
            t.exit_ts = c.start_time();
            t.entry_price = entry_price_;
            t.exit_price = px;
            t.qty = qty_;
            t.pnl = (px - entry_price_) * qty_ - 2.0 * cfg_.fee_per_trade;
            trades_.push_back(t);

            qty_ = 0.0;
            entry_price_ = 0.0;
        }
    }

    void Backtester::update_drawdown(const Candle &c)
    {
        const double equity = cash_ + qty_ * c.close().value();
        if (equity > equity_peak_)
        {
            equity_peak_ = equity;
        }
        else if (equity_peak_ > 0.0)
        {
            const double dd = (equity_peak_ - equity) / equity_peak_ * 100.0;
            if (dd > max_drawdown_pct_)
                max_drawdown_pct_ = dd;
        }
    }

    void Backtester::on_candle(const Candle &c, std::optional<double> prediction)
    {
        ema_fast_.update(c);
        ema_slow_.update(c);
        rsi_.update(c);
        auto e_fast = ema_fast_.is_ready() ? std::optional<double>(ema_fast_.value()) : std::nullopt;
        auto e_slow = ema_slow_.is_ready() ? std::optional<double>(ema_slow_.value()) : std::nullopt;
        auto r = rsi_.is_ready() ? std::optional<double>(rsi_.value()) : std::nullopt;

        IndicatorsSnapshot snap = make_snapshot(c);
        snap.ema_fast = e_fast;
        snap.ema_slow = e_slow;
        snap.rsi = r;

        Signal sig = engine_.eval(snap, prediction);
        apply_signal(c, sig);
        update_drawdown(c);
    }

    Metrics Backtester::finalize()
    {
        if (qty_ > 0.0 && last_close_ > 0.0)
        {
            cash_ += last_close_ * qty_ - cfg_.fee_per_trade;
            Trade t{};
            t.entry_ts = entry_ts_;
            t.exit_ts = last_ts_;
            t.entry_price = entry_price_;
            t.exit_price = last_close_;
            t.qty = qty_;
            t.pnl = (last_close_ - entry_price_) * qty_ - 2.0 * cfg_.fee_per_trade;
            trades_.push_back(t);
            qty_ = 0.0;
            entry_price_ = 0.0;
        }

        if (last_close_ > 0.0)
        {
            Candle pseudo{last_ts_, fin::core::Price(last_close_), fin::core::Price(last_close_), fin::core::Price(last_close_), fin::core::Price(last_close_), fin::core::Volume(0)};
            update_drawdown(pseudo);
        }

        Metrics m{};
        m.final_cash = cash_;
        m.pnl = cash_ - cfg_.initial_cash;
        m.return_pct = (cfg_.initial_cash > 0.0) ? (m.pnl / cfg_.initial_cash * 100.0) : 0.0;
        m.trades = static_cast<int>(trades_.size());
        m.max_drawdown = max_drawdown_pct_;
        for (const auto &t : trades_)
        {
            if (t.pnl >= 0)
                ++m.wins;
            else
                ++m.losses;
        }
        return m;
    }
}
