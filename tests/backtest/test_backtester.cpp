#include "catch2_compat.hpp"
#include "fin/backtest/Backtester.hpp"
#include "fin/core/Candle.hpp"
#include "fin/core/Timestamp.hpp"
using namespace fin;


static core::Timestamp ts_from_ms(long long ms)
{
    using namespace std::chrono;
    return core::Timestamp(nanoseconds(ms * 1'000'000LL));
}

TEST_CASE("Backtester runs and produces trades on rising/falling series", "[backtest]")
{
    backtest::BacktestConfig cfg{};
    cfg.initial_cash = 10000.0;
    cfg.trade_qty = 1.0;
    cfg.fee_per_trade = 0.0;
    cfg.ema_fast = 3;
    cfg.ema_slow = 5;
    cfg.rsi_period = 3;
    signal::SignalEngine eng{};
    backtest::Backtester bt(cfg, eng);
    // Rising then falling prices over 10 candles
    double prices[] = {100, 101, 102, 103, 104, 103, 102, 101, 100, 99};
    long long base = 1693492800000LL;
    for (int i = 0; i < 10; ++i)
    {
        core::Candle c{ts_from_ms(base + i * 60000), core::Price(prices[i]), core::Price(prices[i]), core::Price(prices[i]), core::Price(prices[i]), core::Volume(1)};
        bt.on_candle(c);
    }
    auto m = bt.finalize();
    // We don't assert exact PnL, but expect at least one trade and metrics sane
    REQUIRE(m.trades >= 1);
    REQUIRE(m.final_cash > 0.0);
}
