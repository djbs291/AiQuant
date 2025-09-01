#include "fin/io/Sources.hpp"
#include <fstream>
#include <sstream>
#include <charconv>
#include <memory>
#include <vector>
#include <string_view>
#include <chrono>

using namespace std::string_view_literals;

namespace fin::io
{
    using core::Price;
    using core::Symbol;
    using core::Tick;
    using core::Timestamp;
    using core::Volume;

    static Timestamp from_epoch_ms(long long ms)
    {
        using namespace std::chrono;
        return Timestamp(time_point<system_clock, nanoseconds>(nanoseconds{ms * 1'000'000LL}));
    }

    // MVP: expect epoch millis; ISO8601 can be added later if needed.
    struct FileTickSource::Impl
    {
        std::ifstream in;
        TickCsvOptions opt;
        std::string line;
        std::vector<std::string> headers;
        int idx_ts = -1, idx_sym = -1, idx_price = -1, idx_vol = -1;
        bool header_checked = false;

        explicit Impl(std::string path, TickCsvOptions o) : in(path), opt(o) {}
    };

    FileTickSource::FileTickSource(std::string path, TickCsvOptions opt)
        : impl_(std::make_unique<Impl>(std::move(path), opt)) {}

    // ---- dtor OUT-OF-LINE (critical) ----
    FileTickSource::~FileTickSource() = default;

    static std::vector<std::string> split_line(const std::string &s, char delim)
    {
        std::vector<std::string> out;
        std::string item;
        std::istringstream iss(s);
        while (std::getline(iss, item, delim))
            out.push_back(std::move(item));
        return out;
    }

    static int find_idx(const std::vector<std::string> &hdrs, const std::string &name)
    {
        for (int i = 0; i < (int)hdrs.size(); ++i)
            if (hdrs[i] == name)
                return i;
        return -1;
    }

    std::optional<Tick> FileTickSource::next()
    {
        auto &I = *impl_;
        if (!I.in.good())
            return std::nullopt;

        while (std::getline(I.in, I.line))
        {
            ++stats_.rows;
            // Header detection
            if (!I.header_checked)
            {
                if (I.opt.has_header)
                {
                    I.headers = split_line(I.line, I.opt.delimiter);
                    I.idx_ts = find_idx(I.headers, I.opt.ts_col);
                    I.idx_sym = find_idx(I.headers, I.opt.symbol_col);
                    I.idx_price = find_idx(I.headers, I.opt.price_col);
                    I.idx_vol = find_idx(I.headers, I.opt.volume_col);
                    I.header_checked = true;
                    // fallthrough to read next physical line
                    continue;
                }
                else
                {
                    // No header; assume fixed order: ts, symbol, price, volume
                    I.idx_ts = 0;
                    I.idx_sym = 1;
                    I.idx_price = 2;
                    I.idx_vol = 3;
                    I.header_checked = true;
                    // Proceed to parse this first line as data
                }
            }

            auto cols = split_line(I.line, I.opt.delimiter);
            if (std::max({I.idx_ts, I.idx_sym, I.idx_price, I.idx_vol}) >= (int)cols.size())
            {
                ++stats_.skipped;
                continue;
            }

            // Parse ts (epoch ms MVP)
            long long ms = 0;
            {
                const auto &s = cols[I.idx_ts];
                auto *begin = s.data();
                auto *end = s.data() + s.size();
                if (auto [p, ec] = std::from_chars(begin, end, ms); ec != std::errc{})
                {
                    ++stats_.skipped;
                    continue;
                }
            }
            Timestamp ts = from_epoch_ms(ms);

            // Symbol, Price, Volume
            const std::string &sym = cols[I.idx_sym];

            double price_d = 0.0, vol_d = 0.0;
            {
                const auto &s = cols[I.idx_price];
                auto *b = s.data();
                auto *e = s.data() + s.size();
                if (auto [p, ec] = std::from_chars(b, e, price_d); ec != std::errc{})
                {
                    ++stats_.skipped;
                    continue;
                }
            }
            {
                const auto &s = cols[I.idx_vol];
                auto *b = s.data();
                auto *e = s.data() + s.size();
                if (auto [p, ec] = std::from_chars(b, e, vol_d); ec != std::errc{})
                {
                    ++stats_.skipped;
                    continue;
                }
            }
            ++stats_.parsed;

            return Tick{ts, Symbol{sym}, Price{price_d}, Volume{vol_d}};
        }
        return std::nullopt; // EOF
    }
} // namespace fin::io
