#pragma once
#ifndef FIN_IO_MOCK_TICK_SOURCE_HPP
#define FIN_IO_MOCK_TICK_SOURCE_HPP

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "fin/core/Tick.hpp"
#include "fin/io/Sources.hpp"

namespace fin::io
{
    /**
     * @brief Replays a fixed sequence of ticks.
     *
     * The second ITickSource the IO layer design names, beside FileTickSource. It lets the
     * streaming path be driven without a file on disk, and `rewind()` makes an A/B run over
     * the very same ticks cheap — which is how the stream/batch equivalence test avoids
     * re-reading and re-parsing a CSV to compare two runs.
     */
    class MockTickSource final : public ITickSource
    {
    public:
        explicit MockTickSource(std::vector<fin::core::Tick> ticks)
            : ticks_(std::move(ticks)) {}

        std::optional<fin::core::Tick> next() override
        {
            if (index_ >= ticks_.size())
                return std::nullopt; // stays exhausted however often it is asked again
            return ticks_[index_++];
        }

        void rewind() noexcept { index_ = 0; }

        [[nodiscard]] std::size_t size() const noexcept { return ticks_.size(); }
        [[nodiscard]] std::size_t remaining() const noexcept { return ticks_.size() - index_; }

    private:
        std::vector<fin::core::Tick> ticks_;
        std::size_t index_ = 0;
    };

} // namespace fin::io

#endif // FIN_IO_MOCK_TICK_SOURCE_HPP
