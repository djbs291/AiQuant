#pragma once
#ifndef FIN_IO_TICK_SOURCE_HPP
#define FIN_TO_TICK_SOURCE_HPP

#include <optional>
#include <string>
#include <memory>
#include "fin/io/Options.hpp"
#include "fin/core/Tick.hpp"

namespace fin::io
{
    template <class T>
    struct ISource
    {
        virtual ~ISource() = default;
        virtual std::optional<T> next() = 0; // nullopt => EOF
    };

    struct ReadStats
    {
        std::size_t rows = 0, parsed = 0, skipped = 0;
    };

    class FileTickSource : public ISource<fin::core::Tick>
    {
    public:
        FileTickSource(std::string path, TickCsvOptions opt = {});
        ~FileTickSource();
        std::optional<fin::core::Tick> next() override;
        const ReadStats &stats() const { return stats_; }

    private:
        struct Impl; // PIMPL keeps headers clean
        std::unique_ptr<Impl> impl_;
        ReadStats stats_{};
    };

} // namespace fin::io

#endif // FIN_IO_TICK_SOURCE_HPP
