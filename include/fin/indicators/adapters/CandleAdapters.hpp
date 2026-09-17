#pragma once
#ifndef FIN_INDICATORS_ADAPTERS_CANDLE_ADAPTERS_HPP
#define FIN_INDICATORS_ADAPTERS_CANDLE_ADAPTERS_HPP

#include <optional>
#include "fin/indicators/IIndicatorCandle.hpp"

// concrete indicators
#include "fin/indicators/SMA.hpp"
#include "fin/indicators/EMA.hpp"
#include "fin/indicators/RSI.hpp"
#include "fin/indicators/ZScore.hpp"
#include "fin/indicators/Momentum.hpp"
#include "fin/indicators/MACD.hpp"
#include "fin/indicators/BollingerBands.hpp"
#include "fin/indicators/ATR.hpp"
#include "fin/indicators/ADX.hpp"
#include "fin/indicators/Stochastic.hpp"
#include "fin/indicators/VWAP.hpp"

namespace fin::indicators
{

    // ---------- SMA (uses close) ----------
    class SMAFromCandle final : public IIndicatorScalarCandle
    {
    public:
        explicit SMAFromCandle(std::size_t period) : sma_(period) {}
        void update(const Candle &c) override { last_ = sma_.update(c.close().value()); }
        bool is_ready() const override { return last_.has_value(); }
        double value() const override { return *last_; }
        void reset() override
        {
            sma_.reset();
            last_.reset();
        }

    private:
        SMA sma_;
        std::optional<double> last_;
    };

    // ---------- EMA (uses close) ----------
    class EMAFromCandle final : public IIndicatorScalarCandle
    {
    public:
        explicit EMAFromCandle(std::size_t period) : ema_(period) {}
        void update(const Candle &c) override { last_ = ema_.update(c.close().value()); }
        bool is_ready() const override { return last_.has_value(); }
        double value() const override { return *last_; }
        void reset() override
        {
            ema_.reset();
            last_.reset();
        }

    private:
        EMA ema_;
        std::optional<double> last_;
    };

    // ---------- RSI (uses close) ----------
    class RSIFromCandle final : public IIndicatorScalarCandle
    {
    public:
        explicit RSIFromCandle(std::size_t period = 14) : rsi_(period) {}
        void update(const Candle &c) override
        {
            rsi_.update(c.close());
            last_ = rsi_.is_ready() ? std::optional<double>(rsi_.value()) : std::nullopt;
        }
        bool is_ready() const override { return last_.has_value(); }
        double value() const override { return *last_; }
        void reset() override
        {
            rsi_.reset();
            last_.reset();
        }

    private:
        RSI rsi_;
        std::optional<double> last_;
    };

    // ---------- ZScore (uses close) ----------
    class ZScoreFromCandle final : public IIndicatorScalarCandle
    {
    public:
        explicit ZScoreFromCandle(std::size_t period = 20) : zs_(period) {}
        void update(const Candle &c) override { last_ = zs_.update(c.close().value()); }
        bool is_ready() const override { return last_.has_value(); }
        double value() const override { return *last_; }
        void reset() override
        {
            zs_.reset();
            last_.reset();
        }

    private:
        ZScore zs_;
        std::optional<double> last_;
    };

    // ---------- Momentum (uses close) ----------
    class MomentumFromCandle final : public IIndicatorScalarCandle
    {
    public:
        explicit MomentumFromCandle(std::size_t period = 10,
                                    Momentum::Mode mode = Momentum::Mode::Difference)
            : mom_(period, mode) {}
        void update(const Candle &c) override { last_ = mom_.update(c.close().value()); }
        bool is_ready() const override { return last_.has_value(); }
        double value() const override { return *last_; }
        void reset() override
        {
            mom_.reset();
            last_.reset();
        }

    private:
        Momentum mom_;
        std::optional<double> last_;
    };

    // ---------- MACD (scalar = histogram, uses close) ----------
    class MACDHistFromCandle final : public IIndicatorScalarCandle
    {
    public:
        MACDHistFromCandle(std::size_t fast = 12, std::size_t slow = 26, std::size_t signal = 9)
            : macd_(fast, slow, signal) {}
        void update(const Candle &c) override { pack_ = macd_.update(c.close().value()); }
        bool is_ready() const override { return pack_.has_value(); }
        double value() const override { return pack_->hist; }
        void reset() override
        {
            macd_.reset();
            pack_.reset();
        }

    private:
        MACD macd_;
        std::optional<MACDValue> pack_;
    };

    // ---------- Bollinger (scalar = middle, uses close) ----------
    class BollingerMidFromCandle final : public IIndicatorScalarCandle
    {
    public:
        BollingerMidFromCandle(std::size_t period = 20, double k = 2.0) : bb_(period, k) {}
        void update(const Candle &c) override { bands_ = bb_.update(c.close().value()); }
        bool is_ready() const override { return bands_.has_value(); }
        double value() const override { return bands_->middle; }
        void reset() override
        {
            bb_.reset();
            bands_.reset();
        }

    private:
        BollingerBands bb_;
        std::optional<BollingerBands::Bands> bands_;
    };

    // ---------- ATR (uses H/L/C) ----------
    class ATRFromCandle final : public IIndicatorScalarCandle
    {
    public:
        explicit ATRFromCandle(std::size_t period = 14) : atr_(period) {}
        void update(const Candle &c) override
        {
            last_ = atr_.update(c.high().value(), c.low().value(), c.close().value());
        }
        bool is_ready() const override { return last_.has_value(); }
        double value() const override { return *last_; }
        void reset() override
        {
            atr_.reset();
            last_.reset();
        }

    private:
        ATR atr_;
        std::optional<double> last_;
    };

    // ---------- ADX (scalar = adx, uses H/L/C) ----------
    class ADXFromCandle final : public IIndicatorScalarCandle
    {
    public:
        explicit ADXFromCandle(std::size_t period = 14) : adx_(period) {}
        void update(const Candle &c) override
        {
            pack_ = adx_.update(c.high().value(), c.low().value(), c.close().value());
        }
        bool is_ready() const override { return pack_.has_value(); }
        double value() const override { return pack_->adx; }
        void reset() override
        {
            adx_.reset();
            pack_.reset();
        }

    private:
        ADX adx_;
        std::optional<ADXOut> pack_;
    };

    // ---------- Stochastic (scalar = %K, uses H/L/C) ----------
    class StochKFromCandle final : public IIndicatorScalarCandle
    {
    public:
        StochKFromCandle(std::size_t k = 14, std::size_t d = 3) : st_(k, d) {}
        void update(const Candle &c) override
        {
            last_ = st_.update(c.high().value(), c.low().value(), c.close().value());
        }
        bool is_ready() const override { return last_.has_value(); }
        double value() const override { return last_->k; }
        void reset() override
        {
            st_.reset();
            last_.reset();
        }

    private:
        Stochastic st_;
        std::optional<StochOut> last_;
    };

    // ---------- VWAP (uses TP & volume) ----------
    class VWAPFromCandle final : public IIndicatorScalarCandle
    {
    public:
        VWAPFromCandle() = default;
        void update(const Candle &c) override
        {
            const double tp = (c.high().value() + c.low().value() + c.close().value()) / 3.0;
            last_ = vwap_.update(tp, c.volume().value());
        }
        bool is_ready() const override { return last_.has_value(); } // available from first
        double value() const override { return *last_; }
        void reset() override
        {
            vwap_.reset_session();
            last_.reset();
        }

    private:
        VWAP vwap_;
        std::optional<double> last_;
    };

    // ---------- Close (pseudo-indicator: the raw close, ready from the first candle) ----------
    class CloseFromCandle final : public IIndicatorScalarCandle
    {
    public:
        void update(const Candle &c) override { last_ = c.close().value(); }
        bool is_ready() const override { return last_.has_value(); }
        double value() const override { return *last_; }
        void reset() override { last_.reset(); }

    private:
        std::optional<double> last_;
    };

    // The adapters above expose one scalar per indicator, which left the other outputs of the
    // multi-output indicators unreachable. These cover the rest, so every value a feature set
    // can name has an adapter.

    // ---------- MACD line and signal (uses close) ----------
    class MACDLineFromCandle final : public IIndicatorScalarCandle
    {
    public:
        MACDLineFromCandle(std::size_t fast = 12, std::size_t slow = 26, std::size_t signal = 9)
            : macd_(fast, slow, signal) {}
        void update(const Candle &c) override { pack_ = macd_.update(c.close().value()); }
        bool is_ready() const override { return pack_.has_value(); }
        double value() const override { return pack_->macd; }
        void reset() override
        {
            macd_.reset();
            pack_.reset();
        }

    private:
        MACD macd_;
        std::optional<MACDValue> pack_;
    };

    class MACDSignalFromCandle final : public IIndicatorScalarCandle
    {
    public:
        MACDSignalFromCandle(std::size_t fast = 12, std::size_t slow = 26, std::size_t signal = 9)
            : macd_(fast, slow, signal) {}
        void update(const Candle &c) override { pack_ = macd_.update(c.close().value()); }
        bool is_ready() const override { return pack_.has_value(); }
        double value() const override { return pack_->signal; }
        void reset() override
        {
            macd_.reset();
            pack_.reset();
        }

    private:
        MACD macd_;
        std::optional<MACDValue> pack_;
    };

    // ---------- Bollinger upper and lower bands (uses close) ----------
    class BollingerUpperFromCandle final : public IIndicatorScalarCandle
    {
    public:
        BollingerUpperFromCandle(std::size_t period = 20, double k = 2.0) : bb_(period, k) {}
        void update(const Candle &c) override { bands_ = bb_.update(c.close().value()); }
        bool is_ready() const override { return bands_.has_value(); }
        double value() const override { return bands_->upper; }
        void reset() override
        {
            bb_.reset();
            bands_.reset();
        }

    private:
        BollingerBands bb_;
        std::optional<BollingerBands::Bands> bands_;
    };

    class BollingerLowerFromCandle final : public IIndicatorScalarCandle
    {
    public:
        BollingerLowerFromCandle(std::size_t period = 20, double k = 2.0) : bb_(period, k) {}
        void update(const Candle &c) override { bands_ = bb_.update(c.close().value()); }
        bool is_ready() const override { return bands_.has_value(); }
        double value() const override { return bands_->lower; }
        void reset() override
        {
            bb_.reset();
            bands_.reset();
        }

    private:
        BollingerBands bb_;
        std::optional<BollingerBands::Bands> bands_;
    };

    // ---------- Directional indicators (uses H/L/C) ----------
    // Note: ADXOut only starts being emitted once the ADX itself is seeded (bar 2N-1), so these
    // become ready later than TA-Lib's +DI/-DI, which are available from bar N.
    class ADXPlusDIFromCandle final : public IIndicatorScalarCandle
    {
    public:
        explicit ADXPlusDIFromCandle(std::size_t period = 14) : adx_(period) {}
        void update(const Candle &c) override
        {
            pack_ = adx_.update(c.high().value(), c.low().value(), c.close().value());
        }
        bool is_ready() const override { return pack_.has_value(); }
        double value() const override { return pack_->plusDI; }
        void reset() override
        {
            adx_.reset();
            pack_.reset();
        }

    private:
        ADX adx_;
        std::optional<ADXOut> pack_;
    };

    class ADXMinusDIFromCandle final : public IIndicatorScalarCandle
    {
    public:
        explicit ADXMinusDIFromCandle(std::size_t period = 14) : adx_(period) {}
        void update(const Candle &c) override
        {
            pack_ = adx_.update(c.high().value(), c.low().value(), c.close().value());
        }
        bool is_ready() const override { return pack_.has_value(); }
        double value() const override { return pack_->minusDI; }
        void reset() override
        {
            adx_.reset();
            pack_.reset();
        }

    private:
        ADX adx_;
        std::optional<ADXOut> pack_;
    };

    // ---------- Stochastic %D (uses H/L/C) ----------
    class StochDFromCandle final : public IIndicatorScalarCandle
    {
    public:
        StochDFromCandle(std::size_t k = 14, std::size_t d = 3) : st_(k, d) {}
        void update(const Candle &c) override
        {
            last_ = st_.update(c.high().value(), c.low().value(), c.close().value());
        }
        bool is_ready() const override { return last_.has_value(); }
        double value() const override { return last_->d; }
        void reset() override
        {
            st_.reset();
            last_.reset();
        }

    private:
        Stochastic st_;
        std::optional<StochOut> last_;
    };

} // namespace fin::indicators

#endif // FIN_INDICATORS_ADAPTERS_CANDLE_ADAPTERS_HPP
