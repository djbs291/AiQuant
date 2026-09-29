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

namespace
{
    // RSI neutral, fast EMA above slow: the rules alone say Buy with score +1.
    signal::IndicatorsSnapshot one_rule_buy()
    {
        signal::IndicatorsSnapshot s{};
        s.close = 100.0;
        s.rsi = 50.0;
        s.ema_fast = 101.0;
        s.ema_slow = 99.0;
        return s;
    }

    signal::SignalEngine with_weight(double weight)
    {
        signal::SignalEngineConfig cfg{};
        cfg.model_weight = weight;
        return signal::SignalEngine{cfg};
    }
}

TEST_CASE("The model's vote is worth model_weight against 1 per rule", "[signal][model_weight]")
{
    const auto snap = one_rule_buy();

    // At the default 0.5 a disagreeing model cannot overrule one rule...
    auto sig = with_weight(0.5).eval(snap, -1.0);
    REQUIRE(sig.type == signal::SignalType::Buy);
    REQUIRE(sig.score == Approx(0.5));
    REQUIRE_FALSE(sig.model_decisive);

    // ...above 1 it can, and the signal says so.
    sig = with_weight(1.5).eval(snap, -1.0);
    REQUIRE(sig.type == signal::SignalType::Sell);
    REQUIRE(sig.score == Approx(-0.5));
    REQUIRE(sig.model_decisive);

    // Agreeing with the rules is never decisive, whatever the weight.
    sig = with_weight(3.0).eval(snap, 2.0);
    REQUIRE(sig.type == signal::SignalType::Buy);
    REQUIRE_FALSE(sig.model_decisive);
}

TEST_CASE("At the default weight the model breaks a tie", "[signal][model_weight]")
{
    // RSI oversold (+1) against a bearish crossover (-1): the rules alone say Hold.
    signal::IndicatorsSnapshot s{};
    s.close = 100.0;
    s.rsi = 20.0;
    s.ema_fast = 99.0;
    s.ema_slow = 101.0;

    const auto rules_only = signal::SignalEngine{}.eval(s);
    REQUIRE(rules_only.type == signal::SignalType::Hold);
    REQUIRE_FALSE(rules_only.model_decisive);

    const auto with_model = signal::SignalEngine{}.eval(s, 0.3);
    REQUIRE(with_model.type == signal::SignalType::Buy);
    REQUIRE(with_model.model_decisive);
}

TEST_CASE("A zero weight leaves the model out of the decision", "[signal][model_weight]")
{
    const auto sig = with_weight(0.0).eval(one_rule_buy(), -5.0);
    REQUIRE(sig.type == signal::SignalType::Buy);
    REQUIRE(sig.score == Approx(1.0));
    REQUIRE_FALSE(sig.model_decisive);
    REQUIRE(sig.source.find("ML") == std::string::npos);
}
