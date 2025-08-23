#include "fin/core/Symbol.hpp"

namespace fin::core
{
    Symbol::Symbol(const std::string &v) : val_(v) {}

    const std::string &Symbol::value() const { return val_; }

    bool Symbol::operator==(const Symbol &other) const
    {
        return val_ == other.val_;
    }

} // namespace fin::cor
