#include "fin/core/Price.hpp"

namespace fin::core
{
    Price::Price(double v) : val_(v) {}

    double Price::value() const { return val_; }

    bool Price::operator==(const Price &other) const { return val_ == other.val_; }
    bool Price::operator<(const Price &other) const { return val_ < other.val_; }
    Price Price::operator-(const Price &other) const { return Price(val_ - other.val_); }
    Price Price::operator+(const Price &other) const { return Price(val_ + other.val_); }

} // namespace fin::core
