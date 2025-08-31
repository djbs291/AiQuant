#pragma once
#ifndef FIN_IO_TICK_SOURCE_HPP
#define FIN_TO_TICK_SOURCE_HPP

#include <optional>
#include "fin/core/Tick.hpp"

namespace fin::io
{
    class ITickSource
    {
    public:
        virtual ~ITickSource() = default;
        // Returns next Tick or nullopt at EOF. Blocking or non-Blocking is impl-defined.
        virtual std::optional<Tick> next() = 0;
        virtual void reset() {} // optional
    };

} // namespace fin::io

#endif // FIN_IO_TICK_SOURCE_HPP
