// Detectar automaticamente qual versão do Catch2 usar
#if __has_include(<catch2/catch_test_macros.hpp>)
// Catch2 v3
#include <catch2/catch_test_macros.hpp>
#elif __has_include(<catch2/catch.hpp>)
// Catch2 v2 ou single header na pasta catch2/
#include <catch2/catch.hpp>
#elif __has_include("catch.hpp")
// Single header version na pasta local
#include "catch.hpp"
#elif __has_include(<catch.hpp>)
// Single header version instalado globalmente
#include <catch.hpp>
#else
#error "Nenhum header do Catch2 encontrado!"
#endif

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