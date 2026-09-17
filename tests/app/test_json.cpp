#include "catch2_compat.hpp"

#include <string>

#include "fin/app/Json.hpp"

using fin::app::json::Value;

namespace
{
    Value parse_ok(const std::string &text)
    {
        std::string error;
        auto value = fin::app::json::parse(text, error);
        REQUIRE(value.has_value());
        REQUIRE(error.empty());
        return *value;
    }

    void parse_fails(const std::string &text)
    {
        std::string error;
        auto value = fin::app::json::parse(text, error);
        REQUIRE_FALSE(value.has_value());
        REQUIRE_FALSE(error.empty()); // a refusal must say why
    }
}

TEST_CASE("JSON parses objects, numbers and booleans", "[json]")
{
    const auto value = parse_ok(R"({"close": 100.5, "rsi": 55, "flag": true, "missing": null})");
    REQUIRE(value.is_object());

    const Value *close = value.find("close");
    REQUIRE(close != nullptr);
    REQUIRE(close->is_number());
    REQUIRE(close->as_number() == Approx(100.5).margin(1e-12));

    REQUIRE(value.find("rsi")->as_number() == Approx(55.0).margin(1e-12));
    REQUIRE(value.find("flag")->is_bool());
    REQUIRE(value.find("flag")->as_bool());
    REQUIRE(value.find("missing")->is_null());
    REQUIRE(value.find("absent") == nullptr);
}

TEST_CASE("JSON parses nested objects and arrays", "[json]")
{
    const auto value = parse_ok(R"({"features": {"close": 1.0, "rsi": 2.0}, "names": ["a", "b"]})");

    const Value *features = value.find("features");
    REQUIRE(features != nullptr);
    REQUIRE(features->is_object());
    REQUIRE(features->as_object().size() == 2);
    REQUIRE(features->find("rsi")->as_number() == Approx(2.0).margin(1e-12));

    const Value *names = value.find("names");
    REQUIRE(names != nullptr);
    REQUIRE(names->is_array());
    REQUIRE(names->as_array().size() == 2);
    REQUIRE(names->as_array()[1].as_string() == "b");
}

TEST_CASE("JSON handles numeric forms and empty containers", "[json]")
{
    const auto value = parse_ok(R"({"neg": -2.5, "exp": 1.5e3, "zero": 0, "obj": {}, "arr": []})");
    REQUIRE(value.find("neg")->as_number() == Approx(-2.5).margin(1e-12));
    REQUIRE(value.find("exp")->as_number() == Approx(1500.0).margin(1e-9));
    REQUIRE(value.find("zero")->as_number() == Approx(0.0).margin(1e-12));
    REQUIRE(value.find("obj")->as_object().empty());
    REQUIRE(value.find("arr")->as_array().empty());
}

TEST_CASE("JSON decodes string escapes", "[json]")
{
    const auto value = parse_ok(R"({"s": "a\"b\\c\nd\u0041\u00e9"})");
    REQUIRE(value.find("s")->as_string() == std::string("a\"b\\c\ndA\xc3\xa9"));
}

TEST_CASE("JSON decodes surrogate pairs", "[json]")
{
    const auto value = parse_ok(R"({"s": "\ud83d\ude00"})"); // grinning face, U+1F600
    REQUIRE(value.find("s")->as_string() == std::string("\xf0\x9f\x98\x80"));
}

TEST_CASE("JSON rejects malformed input", "[json]")
{
    parse_fails("");
    parse_fails("{");
    parse_fails("{\"a\": }");
    parse_fails("{\"a\" 1}");
    parse_fails("{a: 1}");
    parse_fails("{\"a\": 1,}");
    parse_fails("{\"a\": 1} trailing");
    parse_fails("{\"a\": 1.2.3}");
    parse_fails("[1, 2");
    parse_fails("tru");
    parse_fails("\"unterminated");
    parse_fails("{\"a\": \"bad \\x escape\"}");
    parse_fails("{\"a\": \"\\ud83d\"}"); // high surrogate with no pair
}

TEST_CASE("JSON refuses to nest deeper than the cap", "[json]")
{
    // Deep input must be refused rather than recursing until the stack gives out.
    const std::size_t depth = fin::app::json::kMaxDepth + 5;
    std::string deep;
    for (std::size_t i = 0; i < depth; ++i)
        deep += "[";
    for (std::size_t i = 0; i < depth; ++i)
        deep += "]";
    parse_fails(deep);

    // Just inside the cap still parses.
    std::string shallow;
    for (std::size_t i = 0; i < fin::app::json::kMaxDepth - 1; ++i)
        shallow += "[";
    shallow += "1";
    for (std::size_t i = 0; i < fin::app::json::kMaxDepth - 1; ++i)
        shallow += "]";
    std::string error;
    REQUIRE(fin::app::json::parse(shallow, error).has_value());
}
