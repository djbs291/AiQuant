#pragma once
#ifndef FIN_CORE_TIMESTAMP_HPP
#define FIN_CORE_TIMESTAMP_HPP

#include <chrono>

namespace fin::core
{
    using Timestamp = std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>;
} // namespace fin::core

#endif // FIN_CORE_TIMESTAMP_HPP
