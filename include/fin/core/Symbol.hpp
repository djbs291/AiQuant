#pragma once
#ifndef FIN_CORE_SYMBOL_HPP
#define FIN_CORE_SYMBOL_HPP

#include <string>
namespace fin::core
{
    class Symbol
    {
    public:
        explicit Symbol(const std::string &v);
        const std::string &value() const;

        bool operator==(const Symbol &other) const;

    private:
        std::string val_;
    };
} // namespace fin::core

#endif // FIN_CORE_SYMBOL_HPP
