#include "fin/app/Json.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>

namespace fin::app::json
{
    const Value *Value::find(std::string_view key) const
    {
        if (!is_object())
            return nullptr;
        const auto &members = as_object();
        const auto it = std::find_if(members.begin(), members.end(),
                                     [key](const auto &member) { return member.first == key; });
        return it == members.end() ? nullptr : &it->second;
    }

    namespace
    {
        class Parser
        {
        public:
            Parser(std::string_view text, std::string &error) : text_(text), error_(error) {}

            std::optional<Value> run()
            {
                skip_whitespace();
                auto value = parse_value(0);
                if (!value)
                    return std::nullopt;
                skip_whitespace();
                if (pos_ != text_.size())
                {
                    fail("unexpected trailing content");
                    return std::nullopt;
                }
                return value;
            }

        private:
            std::string_view text_;
            std::string &error_;
            std::size_t pos_ = 0;

            bool fail(const std::string &what)
            {
                if (error_.empty())
                    error_ = what + " at offset " + std::to_string(pos_);
                return false;
            }

            bool eof() const { return pos_ >= text_.size(); }
            char peek() const { return text_[pos_]; }

            void skip_whitespace()
            {
                while (!eof())
                {
                    const char c = peek();
                    if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                        ++pos_;
                    else
                        break;
                }
            }

            bool literal(std::string_view word)
            {
                if (text_.compare(pos_, word.size(), word) != 0)
                    return fail("invalid literal");
                pos_ += word.size();
                return true;
            }

            std::optional<Value> parse_value(std::size_t depth)
            {
                if (depth > kMaxDepth)
                {
                    fail("nesting deeper than " + std::to_string(kMaxDepth));
                    return std::nullopt;
                }
                if (eof())
                {
                    fail("unexpected end of input");
                    return std::nullopt;
                }

                switch (peek())
                {
                case '{':
                    return parse_object(depth);
                case '[':
                    return parse_array(depth);
                case '"':
                {
                    std::string out;
                    if (!parse_string(out))
                        return std::nullopt;
                    return Value(std::move(out));
                }
                case 't':
                    return literal("true") ? std::optional<Value>(Value(true)) : std::nullopt;
                case 'f':
                    return literal("false") ? std::optional<Value>(Value(false)) : std::nullopt;
                case 'n':
                    return literal("null") ? std::optional<Value>(Value{}) : std::nullopt;
                default:
                    return parse_number();
                }
            }

            std::optional<Value> parse_object(std::size_t depth)
            {
                ++pos_; // '{'
                Object members;
                skip_whitespace();
                if (!eof() && peek() == '}')
                {
                    ++pos_;
                    return Value(std::move(members));
                }

                while (true)
                {
                    skip_whitespace();
                    if (eof() || peek() != '"')
                    {
                        fail("expected a string key");
                        return std::nullopt;
                    }
                    std::string key;
                    if (!parse_string(key))
                        return std::nullopt;

                    skip_whitespace();
                    if (eof() || peek() != ':')
                    {
                        fail("expected ':'");
                        return std::nullopt;
                    }
                    ++pos_;

                    skip_whitespace();
                    auto value = parse_value(depth + 1);
                    if (!value)
                        return std::nullopt;
                    members.emplace_back(std::move(key), std::move(*value));

                    skip_whitespace();
                    if (eof())
                    {
                        fail("unterminated object");
                        return std::nullopt;
                    }
                    if (peek() == ',')
                    {
                        ++pos_;
                        continue;
                    }
                    if (peek() == '}')
                    {
                        ++pos_;
                        return Value(std::move(members));
                    }
                    fail("expected ',' or '}'");
                    return std::nullopt;
                }
            }

            std::optional<Value> parse_array(std::size_t depth)
            {
                ++pos_; // '['
                Array items;
                skip_whitespace();
                if (!eof() && peek() == ']')
                {
                    ++pos_;
                    return Value(std::move(items));
                }

                while (true)
                {
                    skip_whitespace();
                    auto value = parse_value(depth + 1);
                    if (!value)
                        return std::nullopt;
                    items.push_back(std::move(*value));

                    skip_whitespace();
                    if (eof())
                    {
                        fail("unterminated array");
                        return std::nullopt;
                    }
                    if (peek() == ',')
                    {
                        ++pos_;
                        continue;
                    }
                    if (peek() == ']')
                    {
                        ++pos_;
                        return Value(std::move(items));
                    }
                    fail("expected ',' or ']'");
                    return std::nullopt;
                }
            }

            // Appends the UTF-8 encoding of a code point.
            static void append_utf8(std::string &out, std::uint32_t cp)
            {
                if (cp < 0x80)
                {
                    out.push_back(static_cast<char>(cp));
                }
                else if (cp < 0x800)
                {
                    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                else if (cp < 0x10000)
                {
                    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                else
                {
                    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
            }

            bool parse_hex4(std::uint32_t &out)
            {
                if (pos_ + 4 > text_.size())
                    return fail("truncated \\u escape");
                out = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const char c = text_[pos_++];
                    out <<= 4;
                    if (c >= '0' && c <= '9')
                        out |= static_cast<std::uint32_t>(c - '0');
                    else if (c >= 'a' && c <= 'f')
                        out |= static_cast<std::uint32_t>(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F')
                        out |= static_cast<std::uint32_t>(c - 'A' + 10);
                    else
                        return fail("invalid \\u escape");
                }
                return true;
            }

            bool parse_string(std::string &out)
            {
                ++pos_; // opening quote
                out.clear();
                while (true)
                {
                    if (eof())
                        return fail("unterminated string");

                    const char c = text_[pos_++];
                    if (c == '"')
                        return true;

                    if (static_cast<unsigned char>(c) < 0x20)
                        return fail("control character in string");

                    if (c != '\\')
                    {
                        out.push_back(c);
                        continue;
                    }

                    if (eof())
                        return fail("unterminated escape");

                    switch (text_[pos_++])
                    {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u':
                    {
                        std::uint32_t cp = 0;
                        if (!parse_hex4(cp))
                            return false;
                        // Surrogate pair: the low half must follow immediately.
                        if (cp >= 0xD800 && cp <= 0xDBFF)
                        {
                            if (pos_ + 1 >= text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u')
                                return fail("unpaired high surrogate");
                            pos_ += 2;
                            std::uint32_t low = 0;
                            if (!parse_hex4(low))
                                return false;
                            if (low < 0xDC00 || low > 0xDFFF)
                                return fail("invalid low surrogate");
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        }
                        else if (cp >= 0xDC00 && cp <= 0xDFFF)
                        {
                            return fail("unexpected low surrogate");
                        }
                        append_utf8(out, cp);
                        break;
                    }
                    default:
                        return fail("invalid escape");
                    }
                }
            }

            std::optional<Value> parse_number()
            {
                const std::size_t start = pos_;
                if (!eof() && (peek() == '-' || peek() == '+'))
                    ++pos_;
                while (!eof())
                {
                    const char c = peek();
                    if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
                        ++pos_;
                    else
                        break;
                }

                if (start == pos_)
                {
                    fail("expected a value");
                    return std::nullopt;
                }

                double out = 0.0;
                const char *first = text_.data() + start;
                const char *last = text_.data() + pos_;
                const auto [ptr, ec] = std::from_chars(first, last, out);
                if (ec != std::errc{} || ptr != last)
                {
                    pos_ = start;
                    fail("invalid number");
                    return std::nullopt;
                }
                return Value(out);
            }
        };
    }

    std::optional<Value> parse(std::string_view text, std::string &error)
    {
        error.clear();
        Parser parser(text, error);
        auto value = parser.run();
        if (!value && error.empty())
            error = "invalid JSON";
        return value;
    }
}
