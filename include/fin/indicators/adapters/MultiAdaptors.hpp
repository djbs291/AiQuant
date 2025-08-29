#pragma once
#include "fin/indicators/IIndicator.hpp"
#include "fin/indicators/MACD.hpp"
#include "fin/indicators/Stochastic.hpp"
#include "fin/indicators/BollingerBands.hpp"
#include "fin/indicators/ADX.hpp"

namespace fin::indicators
{

    // MACD full {macd, signal, hist}
    class MACDAdapter final : public IIndicatorMultiPrice<MACDValue>
    {
    public:
        MACDAdapter(std::size_t f = 12, std::size_t s = 26, std::size_t sig = 9) : macd_(f, s, sig) {}
        void update(double price) override { last_ = macd_.update(price); }
        bool is_ready() const override { return last_.has_value(); }
        std::optional<MACDValue> value() const override { return last_; }

    private:
        MACD macd_;
        std::optional<MACDValue> last_;
    };

    // Stochastic full {%K,%D}
    class StochasticAdapter final : public IIndicatorMultiOHLC<StochOut>
    {
    public:
        StochasticAdapter(std::size_t k = 14, std::size_t d = 3) : st_(k, d) {}
        void update(double h, double l, double c) override { last_ = st_.update(h, l, c); }
        bool is_ready() const override { return last_.has_value(); }
        std::optional<StochOut> value() const override { return last_; }

    private:
        Stochastic st_;
        std::optional<StochOut> last_;
    };

    // Bollinger full Bands
    class BollingerAdapter final : public IIndicatorMultiPrice<BollingerBands::Bands>
    {
    public:
        BollingerAdapter(std::size_t p = 20, double k = 2.0) : bb_(p, k) {}
        void update(double price) override { last_ = bb_.update(price); }
        bool is_ready() const override { return last_.has_value(); }
        std::optional<BollingerBands::Bands> value() const override { return last_; }

    private:
        BollingerBands bb_;
        std::optional<BollingerBands::Bands> last_;
    };

    // ADX full (+DI,-DI,dx,adx)
    class ADXAdapter final : public IIndicatorMultiOHLC<ADXOut>
    {
    public:
        explicit ADXAdapter(std::size_t p = 14) : adx_(p) {}
        void update(double h, double l, double c) override { last_ = adx_.update(h, l, c); }
        bool is_ready() const override { return last_.has_value(); }
        std::optional<ADXOut> value() const override { return last_; }

    private:
        ADX adx_;
        std::optional<ADXOut> last_;
    };

} // namespace fin::indicators
