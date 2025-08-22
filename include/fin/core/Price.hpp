#pragma once
#ifndef FIN_CORE_PRICE_HPP
#define FIN_CORE_PRICE_HPP

namespace fin::core
{
    class Price
    {
    public:
        explicit Price(double v);
        double value() const;

        bool operator==(const Price &other) const;
        bool operator<(const Price &other) const;
        Price operator-(const Price &other) const;
        Price operator+(const Price &other) const;

    private:
        double val_;
    };
} // namespace fin::core

#endif // FIN_CORE_PRICE_HPP
