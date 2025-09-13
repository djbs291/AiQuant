#include "catch2_compat.hpp"
#include "fin/signal/SignalEngine.hpp"
using namespace fin;

TEST_CASE("SignalEngine basic RSI and EMA logic", "[signal]")
{
    signal::SignalEngine eng{}; // defaults: RSI 30/70, EMA crossover
    signal::IndicatorsSnapshot s{};
    s.close = 100.0;
    s.rsi = 25.0; // oversold
    s.ema_fast = 101.0;
    s.ema_slow = 99.0; // bullish
    auto sig = eng.eval(s);
    REQUIRE(sig.type == signal::SignalType::Buy);
    REQUIRE(sig.score > 0.0);
    // Flip to overbought + bearish crossover
    s.rsi = 80.0;
    s.ema_fast = 98.0;
    s.ema_slow = 100.0;
    sig = eng.eval(s);
    REQUIRE(sig.type == signal::SignalType::Sell);
    REQUIRE(sig.score < 0.0);
}
