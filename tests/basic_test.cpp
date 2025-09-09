#include "catch2_compat.hpp"
#include <string>

// Teste básico para verificar que tudo compila
TEST_CASE("Basic functionality", "[basic]")
{
    REQUIRE(1 + 1 == 2);
    REQUIRE(true);
}

TEST_CASE("String operations", "[basic]")
{
    std::string hello = "Hello";
    std::string world = "World";
    std::string result = hello + " " + world;

    REQUIRE(result == "Hello World");
    REQUIRE(result.length() == 11);
}

// Teste mais específico que pode crescer com o projeto
TEST_CASE("AiQuant basic setup", "[aiquant]")
{
    // Por enquanto só testa que C++20 features funcionam
    auto lambda = []() -> int
    { return 42; };
    REQUIRE(lambda() == 42);

    // Quando adicionar código real, pode testar aqui
    // Por exemplo:
    // REQUIRE(fin::core::some_function() == expected_value);
}