#pragma once
#ifndef FIN_IO_TICK_SOURCE_HPP
#define FIN_IO_TICK_SOURCE_HPP

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

    // The name docs/IOLayerDesign_CppFinancialAIEngine.md gives a tick feed. FileTickSource
    // below already satisfies it, and so will a socket source later, so the streaming engine
    // can be written against this without knowing where its ticks come from.
    //
    // Deliberately no stats() here: ReadStats counts CSV rows, which means nothing to a live
    // feed. A caller that wants them holds the concrete source and asks it directly.
    using ITickSource = ISource<fin::core::Tick>;

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
