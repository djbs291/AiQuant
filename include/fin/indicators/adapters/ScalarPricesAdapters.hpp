#pragma once
#include "fin/indicators/IIndicator.hpp"
#include "fin/indicators/SMA.hpp"
#include "fin/indicators/EMA.hpp"
#include "fin/indicators/RSI.hpp"
#include "fin/indicators/ZScore.hpp"
#include "fin/indicators/Momentum.hpp"
#include "fin/indicators/MACD.hpp" // for hist via IIndicatorScalarPrice
// For RSI which consumes Price
#include "fin/core/Price.hpp"

namespace fin::indicators
{
    // ---------- SMA ----------
    class SMAIndicator final : public IIndicatorScalarPrice
    {
    public:
        explicit SMAIndicator(std::size_t period) : sma_(period) {}
        void update(double price) override { last_ = sma_.update(price); }
        bool is_ready() const override { return last_.has_value(); }
        double value() const override { return *last_; }

    private:
        SMA sma_;
        std::optional<double> last_;
    };

    // ---------- EMA ----------
    class EMAIndicator final : public IIndicatorScalarPrice
    {
    public:
        explicit EMAIndicator(std::size_t period) : ema_(period) {}
        void update(double price) override { last_ = ema_.update(price); }
        bool is_ready() const override { return last_.has_value(); }
        double value() const override { return *last_; }

    private:
        EMA ema_;
        std::optional<double> last_;
    };

    // ---------- RSI ----------
    class RSIIndicator final : public IIndicatorScalarPrice
    {
    public:
        explicit RSIIndicator(std::size_t period = 14) : rsi_(period) {}
        void update(double price) override
        {
            rsi_.update(fin::core::Price(price));
            last_ = rsi_.is_ready() ? std::optional<double>(rsi_.value()) : std::nullopt;
        }
        bool is_ready() const override { return last_.has_value(); }
        double value() const override { return *last_; }

    private:
        RSI rsi_;
        std::optional<double> last_;
    };

    // ---------- ZScore ----------
    class ZScoreIndicator final : public IIndicatorScalarPrice
    {
    public:
        explicit ZScoreIndicator(std::size_t period = 20) : zs_(period) {}
        void update(double price) override { last_ = zs_.update(price); }
        bool is_ready() const override { return last_.has_value(); }
        double value() const override { return *last_; }

    private:
        ZScore zs_;
        std::optional<double> last_;
    };

    // ---------- Momentum (choose Difference or Rate) ----------
    class MomentumIndicator final : public IIndicatorScalarPrice
    {
    public:
        explicit MomentumIndicator(std::size_t period = 10, Momentum::Mode mode = Momentum::Mode::Difference) : mom_(period, mode) {}
        void update(double price) override { last_ = mom_.update(price); }
        bool is_ready() const override { return last_.has_value(); }
        double value() const override { return *last_; }

    private:
        Momentum mom_;
        std::optional<double> last_;
    };

    // ---------- Momentum (Scalar adapter returns histogram) ----------
    class MACDHistIndicator final : public IIndicatorScalarPrice
    {
    public:
        MACDHistIndicator(std::size_t fast = 12, std::size_t slow = 26, std::size_t signal = 9)
            : macd_(fast, slow, signal) {}

        void update(double price) override { last_pack_ = macd_.update(price); }
        bool is_ready() const override { return last_pack_.has_value(); }
        double value() const override { return last_pack_->hist; }

    private:
        MACD macd_;
        std::optional<MACDValue> last_pack_;
    };
} // namespace fin::indicators
