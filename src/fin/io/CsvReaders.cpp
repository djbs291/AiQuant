#include "fin/io/CsvReaders.hpp"
#include <fstream>
#include <charconv>
#include <chrono>
#include <cctype>

namespace fin::io
{
    using namespace fin::core;

    static inline bool is_blank_or_comment(const std::string &s)
    {
        for (char c : s)
        {
            if (c == '#')
                return true;
            if (!std::isspace(static_cast<unsigned char>(c)))
                return false;
        }
        return true;
    }

    Timestamp parse_epoch_ms(std::string_view s)
    {
        long long v = 0;
        std::from_chars(s.data(), s.data() + s.size(), v);
        return Timestamp{std::chrono::milliseconds{v}};
    }

    static void trim_inplace(std::string &s)
    {
        // Find the first char index excluding \t, \r & \n
        auto l = s.find_first_not_of(" \t\r\n");
        // Fin the last char index excluding \t, \r & \n
        auto r = s.find_last_not_of("\t\r\n");
        if (l == std::string::npos)
        {
            s.clear();
            return;
        }
        s = s.substr(l, r - l + 1);
    }

    std::vector<std::string> split_csv_line(const std::string &line)
    {
        std::vector<std::string> out;
        out.reserve(8);
        std::string f;
        f.reserve(32);
        for (char ch : line)
        {
            if (ch == ',')
            {
                trim_inplace(f);
                out.push_back(f);
                f.clear();
            }
            else
                f.push_back(ch);
        }
        trim_inplace(f);
        out.push_back(f);
        return out;
    }

    std::size_t read_ticks_csv(const std::string &path,
                               std::vector<Tick> &out,
                               const TsParser &ts_parser)
    {
        std::ifstream in(path);
        if (!in)
            return 0;

        std::string line;
        bool header_checked = false;
        std::size_t n = 0;

        while (std::getline(in, line))
        {
            if (!header_checked)
            {
                header_checked = true;
                if (!line.empty() && (line.find("timestamp") != std::string::npos || line.find("price") != std::string::npos))
                {
                    continue; // header
                }
            }
            if (line.empty() || is_blank_or_comment(line))
                continue;

            auto f = split_csv_line(line);
            // Expect: timestamp, symbol, price, volume
            if (f.size() < 4)
                continue;

            try
            {
                const Timestamp ts = ts_parser(std::string_view(f[0]));
                const Symbol sym = Symbol{f[1]};
                const Price prc = Price{std::stod(f[2])};
                const Volume vol = Volume{std::stod(f[3])};
                out.emplace_back(ts, sym, prc, vol); // Tick ctor
                ++n;
            }
            catch (...)
            {
                // skip malformed
            }
        }
        return n;
    }

    std::size_t read_candles_csv(const std::string &path,
                                 std::vector<Candle> &out,
                                 const TsParser &ts_parser)
    {
        std::ifstream in(path);
        if (!in)
            return 0;

        std::string line;
        bool header_checked = false;
        std::size_t n = 0;

        while (std::getline(in, line))
        {
            if (!header_checked)
            {
                header_checked = true;
                if (!line.empty() && (line.find("timestamp") != std::string::npos || line.find("open") != std::string::npos))
                {
                    continue; // header
                }
            }
            if (line.empty() || is_blank_or_comment(line))
                continue;

            auto f = split_csv_line(line);

            // Expect: timestamp, open, high, low, close, volume
            if (f.size() < 6)
                continue;

            try
            {
                const Timestamp ts = ts_parser(std::string_view(f[0]));
                const Price o = Price{std::stod(f[1])};
                const Price h = Price{std::stod(f[2])};
                const Price l = Price{std::stod(f[3])};
                const Price c = Price{std::stod(f[4])};
                const Volume v = Volume{std::stod(f[5])};
                out.emplace_back(ts, o, h, l, c, v); // Candle ctor
                ++n;
            }
            catch (...)
            {
                // skip malformed
            }
        }
        return n;
    }

    // FileTickSource
    FileTickSource::FileTickSource(const std::string &path, TsParser ts_parser)
        : path_(path), ts_parser_(std::move(ts_parser)) {}

    void FileTickSource::ensure_open()
    {
        if (!in_)
            in_ = new std::ifstream(path_);
    }

    std::optional<Tick> FileTickSource::parse_line(std::string &line)
    {
        if (line.empty() || is_blank_or_comment(line))
            return std::nullopt;
        auto f = split_csv_line(line);
        if (f.size() < 4)
            return std::nullopt;

        try
        {
            const Timestamp ts = ts_parser_(std::string_view(f[0]));
            const Symbol sym = Symbol{f[1]};
            const Price prc = Price{std::stod(f[2])};
            const Volume vol = Volume{std::stod(f[3])};

            return Tick(ts, sym, prc, vol);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<Tick> FileTickSource::next()
    {
        ensure_open();
        if (!in_ || !*in_)
            return std::nullopt;

        std::string line;
        while (std::getline(*in_, line))
        {
            if (!header_checked_)
            {
                header_checked_ = true;
                if (!line.empty() && (line.find("timestamp") != std::string::npos || line.find("price") != std::string::npos))
                {
                    continue;
                }
            }
            auto t = parse_line(line);
            if (t)
                return t;
        }
        return std::nullopt;
    }

    void FileTickSource::reset()
    {
        if (in_)
        {
            delete in_;
            in_ = nullptr;
        }
        header_checked_ = false;
    }

} // namespace fin::io
