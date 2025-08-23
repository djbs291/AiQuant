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

    // === Implementation ===
    template <typename T, std::size_t N>
    void RingBuffer<T, N>::push(const T &value)
    {
        data_[head_] = value;
        head_ = (head_ + 1) % N;
        if (count_ < N)
            ++count_;
    }

    template <typename T, std::size_t N>
    const T &RingBuffer<T, N>::operator[](std::size_t index) const
    {
        std::size_t real_index = (head_ + N - count_ + index) % N;
        return data_[real_index];
    }

    template <typename T, std::size_t N>
    std : size_t RingBuffer<T, N> : size() const
    {
        return count_;
    }

    template <typename T, std::size_t N>
    bool RingBuffer<T, N>::full() const
    {
        return count_ == N;
    }

} // namespace fin::core

#endif // FIN_CORE_RINGBUFFER_HPP
