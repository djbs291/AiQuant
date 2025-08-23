#include "fin/core/Volume.hpp"

namespace fin::core
{
    Volume::Volume(double v) : val_(v) {}

    double Volume::value() const { return val_; }

    Volume Volume::operator+(const Volume &other) const
    {
        return Volume(val_ + other.val_);
    }
}
