#include "catch2_compat.hpp"

#include <chrono>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "fin/core/Candle.hpp"
#include "fin/indicators/FeatureBus.hpp"
#include "fin/indicators/FeatureSpec.hpp"

using fin::indicators::FeatureBus;
using fin::indicators::FeatureParams;

namespace
{
    // A rising series with a real high/low range, so every indicator has something to chew on.
    std::vector<fin::core::Candle> make_candles(std::size_t n)
    {
        std::vector<fin::core::Candle> candles;
        candles.reserve(n);
        long long ts = 1693492800000LL;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double base = 100.0 + 0.5 * static_cast<double>(i);
            fin::core::Candle c(fin::core::Timestamp{std::chrono::milliseconds{ts}},
                                fin::core::Price(base),
                                fin::core::Price(base + 1.0),
                                fin::core::Price(base - 1.0),
                                fin::core::Price(base + 0.5),
                                fin::core::Volume(10.0));
            candles.push_back(c);
            ts += 60000;
        }
        return candles;
    }
}

TEST_CASE("Feature catalogue resolves known names and rejects unknown ones", "[features][registry]")
{
    REQUIRE(fin::indicators::find_feature("close") != nullptr);
    REQUIRE(fin::indicators::find_feature("macd_signal") != nullptr);
    REQUIRE(fin::indicators::find_feature("plus_di") != nullptr);
    REQUIRE(fin::indicators::find_feature("bogus") == nullptr);
    REQUIRE(fin::indicators::find_feature("") == nullptr);
}

TEST_CASE("Default feature names are the historical six in order", "[features][registry]")
{
    const std::vector<std::string> expected = {
        "close", "ema_fast", "rsi", "macd", "macd_signal", "macd_hist"};
    REQUIRE(fin::indicators::default_feature_names() == expected);
}

TEST_CASE("FeatureBus emits columns in the requested order", "[features][bus]")
{
    const std::vector<std::string> names = {"close", "rsi", "atr"};
    FeatureParams params{};
    params.rsi = 3;
    params.atr = 3;

    FeatureBus bus(names, params);
    REQUIRE(bus.schema().names == names);

    std::vector<fin::indicators::FeatureRow> rows;
    for (const auto &c : make_candles(40))
        if (auto row = bus.update(c))
            rows.push_back(*row);

    REQUIRE(rows.size() > 3);
    const auto &row = rows.back();
    REQUIRE(row.values.size() == names.size());
    REQUIRE(row.schema != nullptr);
    REQUIRE(row.schema->names == names);
    // First column is close, and close is also kept outside values for the training target.
    REQUIRE(row.values[0] == Approx(row.close).margin(1e-12));
}

TEST_CASE("FeatureBus rejects an unknown or empty feature set", "[features][bus]")
{
    FeatureParams params{};
    bool threw = false;
    try
    {
        FeatureBus bus(std::vector<std::string>{"close", "bogus"}, params);
    }
    catch (const std::invalid_argument &)
    {
        threw = true;
    }
    REQUIRE(threw);

    threw = false;
    try
    {
        FeatureBus bus(std::vector<std::string>{}, params);
    }
    catch (const std::invalid_argument &)
    {
        threw = true;
    }
    REQUIRE(threw);
}

TEST_CASE("Every catalogued feature builds and produces finite values", "[features][registry]")
{
    std::vector<std::string> all;
    for (const auto &spec : fin::indicators::feature_catalog())
        all.emplace_back(spec.name);
    REQUIRE(all.size() >= 20);

    FeatureParams params{};
    FeatureBus bus(all, params);

    std::vector<fin::indicators::FeatureRow> rows;
    for (const auto &c : make_candles(200))
        if (auto row = bus.update(c))
            rows.push_back(*row);

    REQUIRE(!rows.empty());
    const auto &row = rows.back();
    REQUIRE(row.values.size() == all.size());
    for (double v : row.values)
        REQUIRE(std::isfinite(v));
}

TEST_CASE("The default FeatureBus constructor keeps the legacy schema", "[features][bus]")
{
    FeatureBus bus(12, 14, 12, 26, 9);
    REQUIRE(bus.schema().names == fin::indicators::default_feature_names());
}
