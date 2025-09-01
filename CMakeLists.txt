cmake_minimum_required(VERSION 3.20)

# Project Definition
project(AiQuant
    VERSION 1.0.0
    DESCRIPTION "Quantitative Finance Library with AI/ML capabilities"
    LANGUAGES CXX
)

# Set C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build Type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# Compiler Warnings and Flags
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

# Include Directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# ============ Core Library ============
file(GLOB_RECURSE CORE_SRC "src/fin/core/*.cpp")
add_library(fin_core STATIC ${CORE_SRC})
target_include_directories(fin_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(fin_core PUBLIC cxx_std_20)

# ============ Indicators Library ============
file(GLOB_RECURSE IND_SRC "src/fin/indicators/*.cpp")
add_library(fin_indicators STATIC ${IND_SRC})
target_include_directories(fin_indicators PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(fin_indicators PUBLIC cxx_std_20)

# ============ IO Library ============
file(GLOB_RECURSE IND_SRC "src/fin/io/*.cpp")
add_library(fin_io STATIC ${IND_SRC})
target_include_directories(fin_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(fin_io PUBLIC cxx_std_20)


# ============ Main Executable ============
add_executable(aiquant src/main.cpp)
target_link_libraries(aiquant PRIVATE fin_core fin_indicators fin_io)
target_compile_features(aiquant PRIVATE cxx_std_20)

# ============ Testing ============
enable_testing()

# Find Catch2
find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    find_package(Catch2 2 QUIET)
endif()
if(NOT Catch2_FOUND)
    find_package(catch2 QUIET)
endif()
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

# Configure Test Executable
file(GLOB_RECURSE TEST_SOURCES "tests/*.cpp" "tests/*.cc" "tests/*.cxx")
if(Catch2_FOUND AND TEST_SOURCES)
    add_executable(aiquant_tests ${TEST_SOURCES})
    add_executable(test_io tests/io/test_tick_to_candle.cpp)
    add_executable(test_resampler tests/io/test_resampler.cpp)
    if(TARGET Catch2::Catch2WithMain)
        target_link_libraries(aiquant_tests PRIVATE Catch2::Catch2WithMain fin_core fin_indicators fin_io)
        target_link_libraries(test_io PRIVATE fin_io fin_core Catch2::Catch2WithMain)
        target_link_libraries(test_resampler PRIVATE fin_io fin_core Catch2::Catch2WithMain)
        message(STATUS "Using Catch2 v3")
    else()
        target_link_libraries(aiquant_tests PRIVATE Catch2::Catch2 fin_core fin_indicators fin_io)
        target_link_libraries(test_io PRIVATE fin_io fin_core Catch2::Catch2)
        target_link_libraries(test_resampler PRIVATE fin_io fin_core Catch2::Catch2)
        message(STATUS "Using Catch2 v2")
    endif()
    target_include_directories(aiquant_tests
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_features(aiquant_tests PRIVATE cxx_std_20)
    include(CTest)
    if(COMMAND catch_discover_tests)
        include(Catch)
        catch_discover_tests(aiquant_tests)
    else()
        add_test(NAME aiquant_tests COMMAND aiquant_tests)
        add_test(NAME test_io COMMAND test_io)
        add_test(NAME test_resampler COMMAND test_resampler)
    endif()
else()
    message(STATUS "Catch2 not found or no test files found. Unit tests disabled.")
endif()

# Build Info Summary
message(STATUS "")
message(STATUS "=== AiQuant Build Configuration ===")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Source: src/main.cpp")
message(STATUS "Tests: make test available")
message(STATUS "=====================================")
message(STATUS "")