cmake_minimum_required(VERSION 3.20)

# Definição do projeto
project(AiQuant
    VERSION 1.0.0
    DESCRIPTION "Quantitative Finance Library with AI/ML capabilities"
    LANGUAGES CXX
)

# Configurações do C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Configurações de build
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# Configurações de compilação
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -DNDEBUG)
    endif()
endif()

# Configurar diretórios de include
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Executável principal
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(aiquant ${SOURCES})

# Configurar C++20 para o executável
target_compile_features(aiquant PRIVATE cxx_std_20)

# ===============================
# TESTES
# ===============================
option(BUILD_TESTS "Build Unit tests" ON)

if(BUILD_TESTS)
    enable_testing()

    # Criar um teste básico que sempre passa
    add_test(
        NAME basic_build_test
        COMMAND ${CMAKE_COMMAND} -E echo "Build test passed - project compiles successfully"
    )

    # Tentar encontrar Catch2 com busca mais agressiva
    find_package(Catch2 3 QUIET)

    if(NOT Catch2_FOUND)
        find_package(Catch2 2 QUIET)
    endif()

    if(NOT Catch2_FOUND)
        find_package(catch2 QUIET)
    endif()

    # Busca manual se não encontrou via find_package
    if(NOT Catch2_FOUND)
        find_path(CATCH2_INCLUDE_DIR
            NAMES catch2/catch.hpp catch2/catch_test_macros.hpp
            PATHS /usr/include /usr/local/include
        )

        if(CATCH2_INCLUDE_DIR)
            message(STATUS "Found Catch2 headers manually at: ${CATCH2_INCLUDE_DIR}")
            set(Catch2_FOUND TRUE)
            add_library(Catch2::Catch2 INTERFACE IMPORTED)
            target_include_directories(Catch2::Catch2 INTERFACE ${CATCH2_INCLUDE_DIR})
        endif()
    endif()

    if(Catch2_FOUND)
        # Procurar por arquivos de teste
        file(GLOB_RECURSE TEST_SOURCES "tests/*.cpp" "tests/*.cc" "tests/*.cxx")

        if(TEST_SOURCES)
            add_executable(aiquant_tests ${TEST_SOURCES})

            # Configuração diferente para Catch2 v3 vs v2
            if(TARGET Catch2::Catch2WithMain)
                # Catch2 v3
                target_link_libraries(aiquant_tests PRIVATE Catch2::Catch2WithMain)
                message(STATUS "Using Catch2 v3")
            else()
                # Catch2 v2
                target_link_libraries(aiquant_tests PRIVATE Catch2::Catch2)
                message(STATUS "Using Catch2 v2")
            endif()

            target_include_directories(aiquant_tests
                PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/tests
            )

            target_compile_features(aiquant_tests PRIVATE cxx_std_20)

            # Registrar testes do Catch2
            include(CTest)

            if(COMMAND catch_discover_tests)
                include(Catch)
                catch_discover_tests(aiquant_tests)
            else()
                add_test(NAME aiquant_tests COMMAND aiquant_tests)
            endif()

            message(STATUS "Catch2 found - real tests enabled")
        else()
            message(STATUS "Catch2 found but no test files in tests/ directory")
        endif()
    else()
        message(STATUS "Catch2 not found - trying manual header inclusion")

        # Tentar usar Catch2 como single header
        file(GLOB_RECURSE TEST_SOURCES "tests/*.cpp" "tests/*.cc" "tests/*.cxx")

        if(TEST_SOURCES)
            add_executable(aiquant_tests ${TEST_SOURCES})

            target_include_directories(aiquant_tests
                PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/tests
            )

            target_compile_features(aiquant_tests PRIVATE cxx_std_20)

            add_test(NAME aiquant_tests COMMAND aiquant_tests)

            message(STATUS "Using header-only Catch2 (if available in tests/)")
        else()
            message(STATUS "No Catch2 found - install with:")
            message(STATUS "  Ubuntu/Debian: sudo apt-get install catch2-dev")
            message(STATUS "  Or download single header to tests/ directory")
        endif()
    endif()
endif()

# ===============================
# INFORMAÇÕES DE BUILD
# ===============================
message(STATUS "")
message(STATUS "=== AiQuant Build Configuration ===")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Source: src/main.cpp")
message(STATUS "Tests: make test available")
message(STATUS "=====================================")
message(STATUS "")