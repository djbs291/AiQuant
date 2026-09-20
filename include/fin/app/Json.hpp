#pragma once
#ifndef FIN_APP_JSON_HPP
#define FIN_APP_JSON_HPP

// A deliberately small JSON reader for the HTTP request bodies.
//
// The project already writes JSON by hand; this is the other direction. It exists because
// /predict and /signal take structured input, and it parses data that arrives over a socket,
// so it is strict by design: it rejects trailing garbage, caps nesting depth, and never
// throws out of parse() — errors come back as a message.

#include <cmath>
#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace fin::app::json
{
    class Value;

    // std::vector tolerates an incomplete element type (C++17), which keeps Value self-
    // referential without an extra indirection. Objects keep insertion order; lookups are a
    // linear scan, which is the right trade for the handful of keys these endpoints take.
    using Object = std::vector<std::pair<std::string, Value>>;
    using Array = std::vector<Value>;

    class Value
    {
    public:
        enum class Type
        {
            Null,
            Bool,
            Number,
            String,
            Object,
            Array
        };

        Value() = default;
        explicit Value(bool b) : storage_(b) {}
        explicit Value(double d) : storage_(d) {}
        explicit Value(std::string s) : storage_(std::move(s)) {}
        explicit Value(Object o) : storage_(std::move(o)) {}
        explicit Value(Array a) : storage_(std::move(a)) {}

        [[nodiscard]] Type type() const noexcept { return static_cast<Type>(storage_.index()); }

        [[nodiscard]] bool is_null() const noexcept { return type() == Type::Null; }
        [[nodiscard]] bool is_bool() const noexcept { return type() == Type::Bool; }
        [[nodiscard]] bool is_number() const noexcept { return type() == Type::Number; }
        [[nodiscard]] bool is_string() const noexcept { return type() == Type::String; }
        [[nodiscard]] bool is_object() const noexcept { return type() == Type::Object; }
        [[nodiscard]] bool is_array() const noexcept { return type() == Type::Array; }

        // Accessors assume the matching type; check with is_*() first.
        [[nodiscard]] bool as_bool() const { return std::get<bool>(storage_); }
        [[nodiscard]] double as_number() const { return std::get<double>(storage_); }
        [[nodiscard]] const std::string &as_string() const { return std::get<std::string>(storage_); }
        [[nodiscard]] const Object &as_object() const { return std::get<Object>(storage_); }
        [[nodiscard]] const Array &as_array() const { return std::get<Array>(storage_); }

        // First member with this key, or nullptr when absent or when this is not an object.
        [[nodiscard]] const Value *find(std::string_view key) const;

    private:
        // Order must match Type.
        std::variant<std::monostate, bool, double, std::string, Object, Array> storage_;
    };

    // Deepest nesting accepted, so a crafted body cannot exhaust the stack.
    inline constexpr std::size_t kMaxDepth = 32;

    // Parses one complete JSON document. Returns std::nullopt and fills `error` when the input
    // is malformed, has trailing content, or nests deeper than kMaxDepth.
    std::optional<Value> parse(std::string_view text, std::string &error);

    // The writing side of the same contract.
    //
    // JSON has no way to spell NaN or Infinity, so streaming a non-finite double straight out
    // produces a bare `nan` that is not JSON. Readers then disagree about it: jq accepts it,
    // while Python's json, JavaScript's JSON.parse and the parser above all reject it — so the
    // document is valid or not depending on who reads it, which is the worst of both.
    //
    // A value that JSON cannot represent is written as null. The document stays parseable and
    // the consumer sees "no number here" rather than a silent lie.
    inline void write_number(std::ostream &out, double value)
    {
        if (std::isfinite(value))
            out << value;
        else
            out << "null";
    }
}

#endif // FIN_APP_JSON_HPP
