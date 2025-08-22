#pragma once
#ifndef FIN_CORE_RINGBUFFER_HPP
#define FIN_CORE_RINGBUFFER_HPP

#include <array>
#include <cstddef>

namespace fin::core
{
    template <typename T, std::size_t N>
    class RingBuffer
    {
    public:
        void push(const T &value);
        const T &operator[](std::size_t index) const;
        std::size_t size() const;
        bool full() const;

    private:
        std::array<T, N> data_{};
        std::size_t head_ = 0;
        std::size_t count_ = 0;
    };
} // namespace fin::core

#endif // FIN_CORE_RINGBUFFER_HPP
