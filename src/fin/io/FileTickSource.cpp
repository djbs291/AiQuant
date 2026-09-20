#include "fin/io/Sources.hpp"
#include <fstream>
#include <sstream>
#include <charconv>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>
#include <string_view>
#include <chrono>
#include <cctype>

using namespace std::string_view_literals;

namespace fin::io
{
    using core::Price;
    using core::Symbol;
    using core::Tick;
    using core::Timestamp;
    using core::Volume;

    static std::pair<const char *, const char *> trim(const std::string &s)
    {
        const char *b = s.data();
        const char *e = b + s.size();
        while (b < e && std::isspace(static_cast<unsigned char>(*b)))
            ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(*(e - 1))))
            --e;
        return {b, e};
    }

    // Epoch millis beyond this overflow the nanosecond conversion below. It lands in the year
    // 2262, so no real feed reaches it, but a fuzzed or corrupt file does — and signed
    // overflow is undefined behaviour, not a large number.
    static constexpr long long kMaxEpochMs = std::numeric_limits<long long>::max() / 1'000'000LL;

    static Timestamp from_epoch_ms(long long ms)
    {
        using namespace std::chrono;
        return Timestamp(time_point<system_clock, nanoseconds>(nanoseconds{ms * 1'000'000LL}));
    }

    // Both parsers require the token to be consumed *entirely*. std::from_chars stops at the
    // first character it cannot use and still reports success, so without the `p == e` check
    // "1.5abc" reads as 1.5 and "123xyz" as 123. The INI and JSON parsers in fin_app have
    // always required full consumption; this reader was the odd one out.
    static bool parse_ll_strict(const char *b, const char *e, long long &out)
    {
        if (b == e)
            return false;
        auto [p, ec] = std::from_chars(b, e, out);
        return ec == std::errc{} && p == e;
    }

    static bool parse_double_strict(const char *b, const char *e, double &out)
    {
        if (b == e)
            return false;
        auto [p, ec] = std::from_chars(b, e, out);
        if (ec != std::errc{} || p != e)
            return false;
        // from_chars accepts "nan" and "inf" by the standard's general format. A NaN price
        // is the worst possible value to let through: the resampler compares with `>` and
        // `<`, which are both false for NaN, so the bar's high and low silently keep the
        // wrong values and every indicator downstream is poisoned.
        return std::isfinite(out);
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
        bool unusable = false; // a file-level problem: stop yielding

        explicit Impl(std::string path, TickCsvOptions o) : in(path), opt(o) {}
    };

    FileTickSource::FileTickSource(std::string path, TickCsvOptions opt)
        : impl_(std::make_unique<Impl>(std::move(path), opt))
    {
        if (!impl_->in)
            error_ = "could not open tick file: " + path;
    }

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

    static std::string join(const std::vector<std::string> &items)
    {
        std::string out;
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            if (i > 0)
                out += ",";
            out += items[i];
        }
        return out;
    }

    std::optional<Tick> FileTickSource::next()
    {
        auto &I = *impl_;
        if (I.unusable || !I.in.good())
            return std::nullopt;

        while (std::getline(I.in, I.line))
        {
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

                    // A missing column used to survive the bounds check below, because that
                    // check only compared the *largest* index against the row width: with
                    // `price` absent its index is -1, the maximum comes from the columns that
                    // are present, and cols[-1] then read off the front of the vector.
                    const std::string *missing = nullptr;
                    if (I.idx_ts < 0)
                        missing = &I.opt.ts_col;
                    else if (I.idx_sym < 0)
                        missing = &I.opt.symbol_col;
                    else if (I.idx_price < 0)
                        missing = &I.opt.price_col;
                    else if (I.idx_vol < 0)
                        missing = &I.opt.volume_col;

                    if (missing)
                    {
                        I.unusable = true;
                        error_ = "tick CSV has no '" + *missing + "' column (header: " +
                                 join(I.headers) + ")";
                        return std::nullopt;
                    }

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

            // Counted here, after the header, so `rows == parsed + skipped` holds.
            ++stats_.rows;

            auto cols = split_line(I.line, I.opt.delimiter);
            if (std::max({I.idx_ts, I.idx_sym, I.idx_price, I.idx_vol}) >= (int)cols.size())
            {
                ++stats_.skipped;
                continue;
            }

            // Parse ts (epoch millis, trimmed)
            long long ms = 0;
            {
                auto [b, e] = trim(cols[I.idx_ts]);
                long long x = 0;
                if (!parse_ll_strict(b, e, x))
                {
                    ++stats_.skipped; // empty, not an integer, or trailing junk
                    continue;
                }
                if (x < 0 || x > kMaxEpochMs)
                {
                    ++stats_.skipped; // negative, or would overflow the ns conversion
                    continue;
                }
                ms = x;
            }
            Timestamp ts = from_epoch_ms(ms);

            // Symbol, Price, Volume
            const std::string &sym = cols[I.idx_sym];

            double price_d = 0.0, vol_d = 0.0;
            {
                auto [b, e] = trim(cols[I.idx_price]);
                if (!parse_double_strict(b, e, price_d) || price_d < 0.0)
                {
                    ++stats_.skipped; // unparsable, non-finite, or negative
                    continue;
                }
            }
            {
                auto [b, e] = trim(cols[I.idx_vol]);
                if (!parse_double_strict(b, e, vol_d) || vol_d < 0.0)
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
