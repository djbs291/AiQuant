#pragma once
#ifndef FIN_TO_CSV_READERS_HPP
#define FIN_TO_CSV_READERS_HPP

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <iosfwd>

#include "fin/core/Tick.hpp"
#include "fin/core/Candle.hpp"
#include "fin/core/Timestamp.hpp"
#include "fin/core/Symbol.hpp"
#include "fin/core/Price.hpp"
#include "fin/core/Volume.hpp"

namespace fin::io
{
    // Timestamp parser: converter CSV field -> fin::core::Timestamp
    using TsParser = std::function<fin::core::Timestamp(std::string_view)>;

    // Default parser expects integer milliseconds since epoch
    fin::core::Timestamp parse_epoch_ms(std::string_view s);

    // Minimal CSV split (no quotes in MVP)
    std::vector<std::string> split_csv_line(const std::string &line);

    // Batch readers (return parsed count; skip malformed rows)
    std::size_t read_ticks_csv(const std::string &path,
                               std::vector<fin::core::Tick> &out,
                               const TsParser &ts_parser = parse_epoch_ms);

    std::size_t read_candles_csv(const std::string &path,
                                 std::vector<fin::core::Candle> &out,
                                 const TsParser &ts_parser_ = parse_epoch_ms);

    // Streaming tick source (historical hile replay)
    class FileTickSource
    {
    public:
        explicit FileTickSource(const std::string &path,
                                TsParser ts_parser = parse_epoch_ms);

        // Returns next Tick or nullopt at EOF
        std::optional<fin::core::Tick> next();
        void reset();

    private:
        std::string path_;
        TsParser ts_parser_;
        std::ifstream *in_ = nullptr;
        bool header_checked_ = false;

        void ensure_open();
        std::optional<fin::core::Tick> parse_line(std::string &line);
    };
} // namespace fin::io

#endif // FIN_TO_CSV_READERS_HPP
