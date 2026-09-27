#include "catch2_compat.hpp"

#include <string>
#include <utility>
#include <vector>

#include "fin/indicators/FeatureSpec.hpp"

using fin::indicators::FeatureParams;
using Params = std::vector<std::pair<std::string, double>>;

TEST_CASE("feature_params_for lists exactly the parameters the features read", "[indicators][params]")
{
    FeatureParams params{};
    params.rsi = 10;
    params.atr = 7;
    params.macd_fast = 8;

    // close and vwap read nothing.
    REQUIRE(fin::indicators::feature_params_for({"close", "vwap"}, params).empty());

    // In FeatureParams order (rsi before atr), whatever order the features come in.
    const Params rsi_atr{{"rsi", 10.0}, {"atr", 7.0}};
    REQUIRE(fin::indicators::feature_params_for({"atr", "close", "rsi"}, params) == rsi_atr);

    // Three MACD features share three parameters, each listed once.
    const Params macd{{"macd_fast", 8.0}, {"macd_slow", 26.0}, {"macd_signal", 9.0}};
    REQUIRE(fin::indicators::feature_params_for({"macd", "macd_signal", "macd_hist"}, params) == macd);

    // Bollinger reads its period and its width; ADX's two DI lines read the ADX period.
    const Params bands{{"bb_period", 20.0}, {"bb_k", 2.0}};
    REQUIRE(fin::indicators::feature_params_for({"bb_mid"}, params) == bands);
    const Params adx{{"adx", 14.0}};
    REQUIRE(fin::indicators::feature_params_for({"plus_di", "minus_di"}, params) == adx);

    // The default six read ema_fast, rsi and the MACD three.
    const Params defaults{{"ema_fast", 12.0}, {"rsi", 10.0}, {"macd_fast", 8.0}, {"macd_slow", 26.0}, {"macd_signal", 9.0}};
    REQUIRE(fin::indicators::feature_params_for(fin::indicators::default_feature_names(), params) == defaults);
}

TEST_CASE("set_feature_param takes only values a parameter can have", "[indicators][params]")
{
    FeatureParams params{};
    REQUIRE(fin::indicators::set_feature_param(params, "rsi", 10.0));
    REQUIRE(params.rsi == 10);
    REQUIRE(fin::indicators::set_feature_param(params, "bb_k", 1.5));
    REQUIRE(params.bb_k == Approx(1.5));

    REQUIRE_FALSE(fin::indicators::set_feature_param(params, "nope", 3.0));
    REQUIRE_FALSE(fin::indicators::set_feature_param(params, "rsi", 0.0));
    REQUIRE_FALSE(fin::indicators::set_feature_param(params, "rsi", 10.5));
    REQUIRE_FALSE(fin::indicators::set_feature_param(params, "rsi", -3.0));
    REQUIRE_FALSE(fin::indicators::set_feature_param(params, "rsi", 1e300));
    REQUIRE_FALSE(fin::indicators::set_feature_param(params, "bb_k", 0.0));
    REQUIRE_FALSE(fin::indicators::set_feature_param(params, "bb_k", -1.0));
    // A refusal leaves the value as it was.
    REQUIRE(params.rsi == 10);
    REQUIRE(params.bb_k == Approx(1.5));

    REQUIRE(fin::indicators::get_feature_param(params, "rsi").value_or(0.0) == Approx(10.0));
    REQUIRE_FALSE(fin::indicators::get_feature_param(params, "nope").has_value());
}

TEST_CASE("Every parameter a feature declares is a real key", "[indicators][params]")
{
    // The guard against drift: a feature that reads a new FeatureParams field has to declare
    // it, and the field has to be in the key table, or a model file would leave it out.
    const auto &keys = fin::indicators::feature_param_keys();
    REQUIRE(keys.size() == 15);
    for (const auto &spec : fin::indicators::feature_catalog())
    {
        for (const auto &key : spec.params)
        {
            FeatureParams params{};
            REQUIRE(fin::indicators::get_feature_param(params, key).has_value());
        }
    }
}
