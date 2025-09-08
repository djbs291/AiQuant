#pragma once
#include <optional>

namespace fin::indicators
{

    // Minimal scalar, price-only interface (your original shape)
    class IIndicatorScalarPrice
    {
    public:
        virtual ~IIndicatorScalarPrice() = default;
        virtual void update(double price) = 0;
        virtual bool is_ready() const = 0;
        virtual double value() const = 0; // undefined if !is_ready()
    };

    // For Indicators that require HLC/HL/C (ATR, ADX, Stochrastic, Bollinger)
    class IIndicatorScalarOHLC
    {
    public:
        virtual ~IIndicatorScalarOHLC() = default;
        virtual void update(double high, double low, double close) = 0;
        virtual bool is_ready() const = 0;
        virtual double value() const = 0;
    };

    // For indicators requiring price + volume (VWAP)
    class IIndicatorScalarPV
    {
        virtual ~IIndicatorScalarPV() = default;
        virtual void update(double price, double volume) = 0;
        virtual bool is_ready() const = 0;
        virtual double value() const = 0;
    };

    // Multi-output indicators: expose their native struct via type param
    template <typename T>
    class IIndicatorMultiOHLC
    {
    public:
        virtual ~IIndicatorMultiOHLC() = default;
        virtual void update(double high, double low, double close) = 0;
        virtual bool is_ready() const = 0;
        virtual std::optional<T> value() const = 0;
    };

    template <typename T>
    class IIndicatorMultiPrice
    {
        virtual ~IIndicatorMultiPrice() = default;
        virtual void update(double price) = 0;
        virtual bool is_ready() const = 0;
        virtual std::optional<T> value() const = 0;
    };

} // namespace fin::indicators
