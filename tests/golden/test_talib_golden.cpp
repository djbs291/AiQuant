// Golden tests: our indicators against the TA-Lib C library on the same series.
//
// TA-Lib is the de facto reference for these formulas, so a mismatch here means either a
// bug on our side or a documented convention difference. Where the difference is by design
// it is spelled out in the test that relies on it.

#include "talib_compare.hpp"

#include "fin/core/Price.hpp"
#include "fin/indicators/ADX.hpp"
#include "fin/indicators/ATR.hpp"
#include "fin/indicators/BollingerBands.hpp"
#include "fin/indicators/EMA.hpp"
#include "fin/indicators/MACD.hpp"
#include "fin/indicators/Momentum.hpp"
#include "fin/indicators/RSI.hpp"
#include "fin/indicators/SMA.hpp"
#include "fin/indicators/Stochastic.hpp"
#include "fin/indicators/ZScore.hpp"

using namespace fin::indicators;

namespace
{
    constexpr double kExact = 1e-9;      // same formula, same seeding: expect full agreement
    constexpr double kAsymptotic = 1e-6; // series that converge but seed differently

    struct TaOutput
    {
        std::vector<double> values;
        std::vector<double> second;
        std::vector<double> third;
        int beg_idx = 0;
        int count = 0;

        explicit TaOutput(std::size_t n) : values(n, 0.0), second(n, 0.0), third(n, 0.0) {}
    };
}

TEST_CASE("Golden: SMA matches TA_SMA", "[golden][sma]")
{
    golden::ensure_ta_initialized();
    const auto series = golden::make_series();
    const int n = static_cast<int>(series.close.size());
    const int period = 14;

    TaOutput ta(series.close.size());
    REQUIRE(TA_SMA(0, n - 1, series.close.data(), period, &ta.beg_idx, &ta.count, ta.values.data()) == TA_SUCCESS);
    REQUIRE(ta.beg_idx == TA_SMA_Lookback(period));

    const auto ours = SMA::compute(series.close, period);
    golden::compare_against_talib("SMA", ours, ta.values, ta.beg_idx, ta.count, kExact,
                                  static_cast<std::size_t>(TA_SMA_Lookback(period)));
}

TEST_CASE("Golden: EMA matches TA_EMA", "[golden][ema]")
{
    golden::ensure_ta_initialized();
    const auto series = golden::make_series();
    const int n = static_cast<int>(series.close.size());
    const int period = 14;

    TaOutput ta(series.close.size());
    REQUIRE(TA_EMA(0, n - 1, series.close.data(), period, &ta.beg_idx, &ta.count, ta.values.data()) == TA_SUCCESS);
    REQUIRE(ta.beg_idx == TA_EMA_Lookback(period));

    // Both seed the EMA with an SMA of the first `period` samples.
    const auto ours = EMA::compute(series.close, period);
    golden::compare_against_talib("EMA", ours, ta.values, ta.beg_idx, ta.count, kExact,
                                  static_cast<std::size_t>(TA_EMA_Lookback(period)));
}

TEST_CASE("Golden: RSI matches TA_RSI", "[golden][rsi]")
{
    golden::ensure_ta_initialized();
    const auto series = golden::make_series();
    const int n = static_cast<int>(series.close.size());
    const int period = 14;

    TaOutput ta(series.close.size());
    REQUIRE(TA_RSI(0, n - 1, series.close.data(), period, &ta.beg_idx, &ta.count, ta.values.data()) == TA_SUCCESS);
    REQUIRE(ta.beg_idx == TA_RSI_Lookback(period));

    // RSI has no batch helper and takes core::Price, so drive it incrementally.
    RSI rsi(static_cast<std::size_t>(period));
    std::vector<std::optional<double>> ours;
    ours.reserve(series.close.size());
    for (double close : series.close)
    {
        rsi.update(fin::core::Price(close));
        ours.push_back(rsi.is_ready() ? std::optional<double>(rsi.value()) : std::nullopt);
    }

    // Wilder seeds the averages with the first `period` deltas, so the first RSI lands on
    // bar `period` — same convention as TA_RSI_Lookback.
    golden::compare_against_talib("RSI", ours, ta.values, ta.beg_idx, ta.count, kExact,
                                  static_cast<std::size_t>(TA_RSI_Lookback(period)));
}

TEST_CASE("Golden: ATR matches TA_ATR", "[golden][atr]")
{
    golden::ensure_ta_initialized();
    const auto series = golden::make_series();
    const int n = static_cast<int>(series.close.size());
    const int period = 14;

    TaOutput ta(series.close.size());
    REQUIRE(TA_ATR(0, n - 1, series.high.data(), series.low.data(), series.close.data(), period,
                   &ta.beg_idx, &ta.count, ta.values.data()) == TA_SUCCESS);
    REQUIRE(ta.beg_idx == TA_ATR_Lookback(period));

    const auto ours = ATR::compute(series.high, series.low, series.close, period);
    golden::compare_against_talib("ATR", ours, ta.values, ta.beg_idx, ta.count, kExact,
                                  static_cast<std::size_t>(TA_ATR_Lookback(period)));
}

// The directional family seeds differently on the two sides, so it converges instead of
// matching bar for bar. We follow Wilder's book: ATR, +DM and -DM start as the mean of the
// first `period` bars. TA-Lib (see ta_ADX.c) accumulates `period - 1` bars and then applies a
// smoothing step, so its first value carries one extra decay. Both are defensible readings of
// Wilder, neither is a rounding artefact, and the difference washes out: on this series the
// relative gap is 4e-3 at the first bar, 5e-4 by bar 100, 1e-6 by bar 200 and 8e-10 by bar 300.
// Hence the settled offset below. Each output gets its own test case, because a failing
// REQUIRE ends the case and would otherwise hide the comparisons that follow it.
namespace
{
    constexpr std::size_t kDirectionalSettled = 250;
}

TEST_CASE("Golden: ADX converges to TA_ADX", "[golden][adx]")
{
    golden::ensure_ta_initialized();
    const auto series = golden::make_series();
    const int n = static_cast<int>(series.close.size());
    const int period = 14;

    const auto ours = ADX::compute(series.high, series.low, series.close, period);

    TaOutput adx(series.close.size());
    REQUIRE(TA_ADX(0, n - 1, series.high.data(), series.low.data(), series.close.data(), period,
                   &adx.beg_idx, &adx.count, adx.values.data()) == TA_SUCCESS);
    // Both put the first ADX on bar 2N-1, even though the value differs during warmup.
    REQUIRE(adx.beg_idx == TA_ADX_Lookback(period));

    const auto adx_series = golden::project(ours, [](const ADXOut &v) { return v.adx; });
    golden::compare_against_talib("ADX", adx_series, adx.values, adx.beg_idx, adx.count, kAsymptotic,
                                  static_cast<std::size_t>(TA_ADX_Lookback(period)), kDirectionalSettled);
}

TEST_CASE("Golden: directional indicators converge to TA_PLUS_DI and TA_MINUS_DI", "[golden][adx][di]")
{
    golden::ensure_ta_initialized();
    const auto series = golden::make_series();
    const int n = static_cast<int>(series.close.size());
    const int period = 14;

    const auto ours = ADX::compute(series.high, series.low, series.close, period);

    // TA-Lib emits DI from bar `period`; our ADXOut only carries it once the ADX is seeded.
    TaOutput plus(series.close.size());
    REQUIRE(TA_PLUS_DI(0, n - 1, series.high.data(), series.low.data(), series.close.data(), period,
                       &plus.beg_idx, &plus.count, plus.values.data()) == TA_SUCCESS);
    const auto plus_series = golden::project(ours, [](const ADXOut &v) { return v.plusDI; });
    golden::compare_against_talib("+DI", plus_series, plus.values, plus.beg_idx, plus.count, kAsymptotic,
                                  std::nullopt, kDirectionalSettled);

    TaOutput minus(series.close.size());
    REQUIRE(TA_MINUS_DI(0, n - 1, series.high.data(), series.low.data(), series.close.data(), period,
                        &minus.beg_idx, &minus.count, minus.values.data()) == TA_SUCCESS);
    const auto minus_series = golden::project(ours, [](const ADXOut &v) { return v.minusDI; });
    golden::compare_against_talib("-DI", minus_series, minus.values, minus.beg_idx, minus.count, kAsymptotic,
                                  std::nullopt, kDirectionalSettled);
}

TEST_CASE("Golden: Bollinger Bands match TA_BBANDS", "[golden][bbands]")
{
    golden::ensure_ta_initialized();
    const auto series = golden::make_series();
    const int n = static_cast<int>(series.close.size());
    const int period = 20;
    const double k = 2.0;

    TaOutput ta(series.close.size());
    REQUIRE(TA_BBANDS(0, n - 1, series.close.data(), period, k, k, TA_MAType_SMA,
                      &ta.beg_idx, &ta.count, ta.values.data(), ta.second.data(), ta.third.data()) == TA_SUCCESS);
    REQUIRE(ta.beg_idx == TA_BBANDS_Lookback(period, k, k, TA_MAType_SMA));

    // TA-Lib's band order is upper, middle, lower. Both sides use the population stddev.
    const auto ours = BollingerBands::compute(series.close, period, k);
    const auto first = static_cast<std::size_t>(ta.beg_idx);

    golden::compare_against_talib("BBANDS upper", golden::project(ours, [](const BollingerBands::Bands &b) { return b.upper; }),
                                  ta.values, ta.beg_idx, ta.count, kExact, first);
    golden::compare_against_talib("BBANDS middle", golden::project(ours, [](const BollingerBands::Bands &b) { return b.middle; }),
                                  ta.second, ta.beg_idx, ta.count, kExact, first);
    golden::compare_against_talib("BBANDS lower", golden::project(ours, [](const BollingerBands::Bands &b) { return b.lower; }),
                                  ta.third, ta.beg_idx, ta.count, kExact, first);
}

TEST_CASE("Golden: Momentum matches TA_MOM and TA_ROCP", "[golden][momentum]")
{
    golden::ensure_ta_initialized();
    const auto series = golden::make_series();
    const int n = static_cast<int>(series.close.size());
    const int period = 10;

    TaOutput mom(series.close.size());
    REQUIRE(TA_MOM(0, n - 1, series.close.data(), period, &mom.beg_idx, &mom.count, mom.values.data()) == TA_SUCCESS);
    const auto ours_diff = Momentum::compute(series.close, period, Momentum::Mode::Difference);
    golden::compare_against_talib("MOM", ours_diff, mom.values, mom.beg_idx, mom.count, kExact,
                                  static_cast<std::size_t>(TA_MOM_Lookback(period)));

    // Our Rate mode is close/prev - 1, which is TA-Lib's rate-of-change percentage.
    TaOutput rocp(series.close.size());
    REQUIRE(TA_ROCP(0, n - 1, series.close.data(), period, &rocp.beg_idx, &rocp.count, rocp.values.data()) == TA_SUCCESS);
    const auto ours_rate = Momentum::compute(series.close, period, Momentum::Mode::Rate);
    golden::compare_against_talib("ROCP", ours_rate, rocp.values, rocp.beg_idx, rocp.count, kExact,
                                  static_cast<std::size_t>(TA_ROCP_Lookback(period)));
}

TEST_CASE("Golden: ZScore matches TA_SMA composed with TA_STDDEV", "[golden][zscore]")
{
    golden::ensure_ta_initialized();
    const auto series = golden::make_series();
    const int n = static_cast<int>(series.close.size());
    const int period = 20;

    // TA-Lib has no z-score, so build the reference from its SMA and population stddev.
    TaOutput sma(series.close.size());
    REQUIRE(TA_SMA(0, n - 1, series.close.data(), period, &sma.beg_idx, &sma.count, sma.values.data()) == TA_SUCCESS);

    TaOutput stddev(series.close.size());
    REQUIRE(TA_STDDEV(0, n - 1, series.close.data(), period, 1.0,
                      &stddev.beg_idx, &stddev.count, stddev.values.data()) == TA_SUCCESS);
    REQUIRE(sma.beg_idx == stddev.beg_idx);

    std::vector<double> expected;
    expected.reserve(static_cast<std::size_t>(sma.count));
    for (int k = 0; k < sma.count; ++k)
    {
        const std::size_t bar = static_cast<std::size_t>(sma.beg_idx + k);
        const double sd = stddev.values[static_cast<std::size_t>(k)];
        expected.push_back(sd > 0.0 ? (series.close[bar] - sma.values[static_cast<std::size_t>(k)]) / sd : 0.0);
    }

    const auto ours = ZScore::compute(series.close, period);
    golden::compare_against_talib("ZSCORE", ours, expected, sma.beg_idx, sma.count, kExact,
                                  static_cast<std::size_t>(sma.beg_idx));
}

TEST_CASE("Golden: Stochastic matches TA_STOCHF", "[golden][stochastic]")
{
    golden::ensure_ta_initialized();
    const auto series = golden::make_series();
    const int n = static_cast<int>(series.close.size());
    const int k_period = 14;
    const int d_period = 3;

    // Ours is the fast stochastic (raw %K, SMA %D), which is TA_STOCHF — TA_STOCH adds slowing.
    TaOutput ta(series.close.size());
    REQUIRE(TA_STOCHF(0, n - 1, series.high.data(), series.low.data(), series.close.data(),
                      k_period, d_period, TA_MAType_SMA,
                      &ta.beg_idx, &ta.count, ta.values.data(), ta.second.data()) == TA_SUCCESS);
    REQUIRE(ta.beg_idx == TA_STOCHF_Lookback(k_period, d_period, TA_MAType_SMA));

    const auto ours = Stochastic::compute(series.high, series.low, series.close, k_period, d_period);
    const auto first = static_cast<std::size_t>(ta.beg_idx);

    golden::compare_against_talib("STOCHF %K", golden::project(ours, [](const StochOut &v) { return v.k; }),
                                  ta.values, ta.beg_idx, ta.count, kExact, first);
    golden::compare_against_talib("STOCHF %D", golden::project(ours, [](const StochOut &v) { return v.d; }),
                                  ta.second, ta.beg_idx, ta.count, kExact, first);
}

TEST_CASE("Golden: MACD converges to TA_MACD", "[golden][macd]")
{
    golden::ensure_ta_initialized();
    const auto series = golden::make_series();
    const int n = static_cast<int>(series.close.size());
    const int fast = 12, slow = 26, signal = 9;

    TaOutput ta(series.close.size());
    REQUIRE(TA_MACD(0, n - 1, series.close.data(), fast, slow, signal,
                    &ta.beg_idx, &ta.count, ta.values.data(), ta.second.data(), ta.third.data()) == TA_SUCCESS);
    REQUIRE(ta.beg_idx == TA_MACD_Lookback(fast, slow, signal));

    // Convention difference, not a bug: TA-Lib aligns the fast EMA to the slow EMA's window
    // (it consumes slow-fast bars before accumulating both), while ours seeds the fast EMA at
    // its own lookback and smooths from there. The two series converge as the seed decays, so
    // compare only well past the warmup.
    const std::size_t settled = static_cast<std::size_t>(ta.beg_idx) + 150;

    const auto ours = MACD::compute(series.close, fast, slow, signal);
    golden::compare_against_talib("MACD line", golden::project(ours, [](const MACDValue &v) { return v.macd; }),
                                  ta.values, ta.beg_idx, ta.count, kAsymptotic, std::nullopt, settled);
    golden::compare_against_talib("MACD signal", golden::project(ours, [](const MACDValue &v) { return v.signal; }),
                                  ta.second, ta.beg_idx, ta.count, kAsymptotic, std::nullopt, settled);
    golden::compare_against_talib("MACD hist", golden::project(ours, [](const MACDValue &v) { return v.hist; }),
                                  ta.third, ta.beg_idx, ta.count, kAsymptotic, std::nullopt, settled);
}
