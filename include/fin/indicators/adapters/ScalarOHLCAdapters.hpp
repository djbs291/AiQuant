#pragma once
#include "fin/indicators/IIndicator.hpp"
#include "fin/indicators/ATR.hpp"
#include "fin/indicators/ADX.hpp"
#include "fin/indicators/Stochastic.hpp"
#include "fin/indicators/BollingerBands.hpp"

namespace fin::indicators
{

    // ---------- ATR (scalar = ATR) ----------
    class ATRIndicator final : public IIndicatorScalarOHLC
    {
    public:
        explicit ATRIndicator(std::size_t period = 14) : atr_(period) {}
        void update(double h, double l, double c) override { last_ = atr_.update(h, l, c); }
        bool is_ready() const override { return last_has_value(); }
        double value() const override { return *last_; }

    private:
        ATR atr_;
        std::optional<double> last_;
    };

    // ---------- ADX (scalar = adx) ----------
    class ADXIndicator final : public IIndicatorScalarOHLC
    {
    public:
        explicit ADXIndicator(std::size_t period = 14) : adx_(period) {}
        void update(double h, double l, double c) override { last_ = adx_.update(h, l, c); }
        bool is_ready() const override { return last_.has_value(); }
        double value() const override { return last_->adx; }

    private:
        ADX adx_;
        std::optional<ADXOut> last_;
    };

    // ---------- Stochastic (scalar = %K) ----------
    class StochKIndicator final : public IIndicatorScalarOHLC
    {
    public:
        StochKIndicator(std::size_t k = 14, std::size_t d = 3) : st_(k, d) {}
        void update(double h, double l, double c) override { last_ = st_.update(h, l, c); }
        bool is_ready() const override { return last_.has_value(); }
        double value() const override { return last_->k; }

    private:
        Stochastic st_;
        std::optional<StochOut> last_;
    };

    // ---------- Bollinger (scalar = middle SMA) ----------
    class BollingerMidIndicator final : public IIndicatorScalarOHLC
    {
    public:
        BollingerMidIndicator(std::size_t period = 20, double k = 2.0) : bb_(period, k) {}
        void update(double /*h*/, double /*l*/, double c) override { last_ = bb_.update(c); }
        bool is_ready() const override { return last_.has_value(); }
        double value() const override { return last_->middle; }

    private:
        BollingerBands bb_;
        std::optional<BollingerBands::Bands> last_;
    };

} // namespace fin::indicators
