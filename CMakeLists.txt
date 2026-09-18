cmake_minimum_required(VERSION 3.20)

# Project Definition
project(AiQuant
    VERSION 1.0.0
    DESCRIPTION "Quantitative Finance Library with AI/ML capabilities"
    LANGUAGES CXX
)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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
target_link_libraries(fin_indicators PUBLIC fin_core)

# ============ IO Library ============
file(GLOB_RECURSE IO_SRC "src/fin/io/*.cpp")
add_library(fin_io STATIC ${IO_SRC})
target_include_directories(fin_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(fin_io PUBLIC cxx_std_20)
target_link_libraries(fin_io PUBLIC fin_core)

# ============ Signal Library ============
file(GLOB_RECURSE SIGNAL_SRC "src/fin/signal/*.cpp")
add_library(fin_signal STATIC ${SIGNAL_SRC})
target_include_directories(fin_signal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(fin_signal PUBLIC cxx_std_20)
target_link_libraries(fin_signal PUBLIC fin_core)

# ============ Backtest Library ============
file(GLOB_RECURSE BACKTEST_SRC "src/fin/backtest/*.cpp")
add_library(fin_backtest STATIC ${BACKTEST_SRC})
target_include_directories(fin_backtest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(fin_backtest PUBLIC cxx_std_20)
target_link_libraries(fin_backtest PUBLIC fin_core fin_indicators fin_signal)

# ============ ML Library ============
file(GLOB_RECURSE ML_SRC "src/fin/ml/*.cpp")
add_library(fin_ml STATIC ${ML_SRC})
target_include_directories(fin_ml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(fin_ml PUBLIC cxx_std_20)
target_link_libraries(fin_ml PUBLIC fin_core fin_indicators)

# ============ Streaming Library ============
# Not part of fin_app: fin_app links fin_backtest, and the live path must never depend on the
# backtester. Not part of fin_io either, which is core-only and must not acquire indicators,
# ml or signal.
file(GLOB_RECURSE STREAM_SRC "src/fin/stream/*.cpp")
add_library(fin_stream STATIC ${STREAM_SRC})
target_include_directories(fin_stream PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(fin_stream PUBLIC cxx_std_20)
target_link_libraries(fin_stream PUBLIC fin_io fin_indicators fin_ml fin_signal fin_core)

# ============ Application Library ============
file(GLOB_RECURSE APP_SRC "src/fin/app/*.cpp")
add_library(fin_app STATIC ${APP_SRC})
target_include_directories(fin_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(fin_app PUBLIC cxx_std_20)
target_link_libraries(fin_app PUBLIC fin_io fin_backtest fin_ml fin_signal fin_indicators fin_core)

# ============ API Library ============
file(GLOB_RECURSE API_SRC "src/fin/api/*.cpp")
add_library(fin_api STATIC ${API_SRC})
target_include_directories(fin_api PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(fin_api PUBLIC cxx_std_20)
target_link_libraries(fin_api PUBLIC fin_app)

# ============ Main Executable ============
add_executable(aiquant src/main.cpp)
# fin_api pulls in fin_app and, transitively, every other library (all PUBLIC deps).
# fin_stream is not in that tree — nothing in fin_app uses it — so it is named explicitly.
target_link_libraries(aiquant PRIVATE fin_api fin_stream)
target_compile_features(aiquant PRIVATE cxx_std_20)

add_executable(aiquant_http src/server/http_main.cpp)
find_package(Threads REQUIRED)
target_link_libraries(aiquant_http PRIVATE fin_api Threads::Threads)
target_compile_features(aiquant_http PRIVATE cxx_std_20)

# ============ Python Bindings (optional) ============
option(AIQUANT_BUILD_PYTHON "Build AiQuant python bindings" ON)

if(AIQUANT_BUILD_PYTHON)
    find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
    find_package(pybind11 CONFIG QUIET)
    if(NOT pybind11_FOUND)
        execute_process(
            COMMAND ${Python3_EXECUTABLE} -m pybind11 --cmakedir
            OUTPUT_VARIABLE PYBIND11_CMAKE_DIR
            RESULT_VARIABLE PYBIND11_CMAKE_RESULT
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(PYBIND11_CMAKE_RESULT EQUAL 0 AND PYBIND11_CMAKE_DIR)
            list(APPEND CMAKE_PREFIX_PATH ${PYBIND11_CMAKE_DIR})
            find_package(pybind11 CONFIG QUIET)
        endif()
    endif()
    if(NOT pybind11_FOUND)
        message(FATAL_ERROR "pybind11 not found; install pybind11 (pip install pybind11) or configure -DAIQUANT_BUILD_PYTHON=OFF to skip Python bindings.")
    endif()
    add_library(aiquant_api MODULE bindings/python/aiquant_module.cpp)
    target_link_libraries(aiquant_api PRIVATE fin_api pybind11::module Python3::Python)
    target_compile_features(aiquant_api PRIVATE cxx_std_20)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import sysconfig;print(sysconfig.get_config_var('EXT_SUFFIX') or '.so')"
        OUTPUT_VARIABLE PY_EXT_SUFFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT PY_EXT_SUFFIX)
        set(PY_EXT_SUFFIX ".so")
    endif()
    set_target_properties(aiquant_api PROPERTIES PREFIX "" SUFFIX "${PY_EXT_SUFFIX}")
endif()

# ============ Testing ============
enable_testing()

# Find Catch2
option(AIQUANT_USE_BUNDLED_CATCH "Use the bundled minicatch instead of real Catch2 v3" OFF)

if(NOT AIQUANT_USE_BUNDLED_CATCH)
    include(FetchContent)
    # FIND_PACKAGE_ARGS makes this prefer an installed Catch2 v3 (brew, apt) and download
    # only when there is none, so a machine with the package configures offline.
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.16.0
        FIND_PACKAGE_ARGS 3
    )
    FetchContent_MakeAvailable(Catch2)
endif()

# Offline fallback: the bundled 95-line minicatch. No filtering, no SECTION, one ctest entry.
if(NOT TARGET Catch2::Catch2WithMain AND NOT TARGET Catch2::Catch2
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/catch2/catch.hpp")
    message(STATUS "Using the bundled minicatch headers")
    add_library(Catch2::Catch2 INTERFACE IMPORTED)
    target_include_directories(Catch2::Catch2 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()

if(TARGET Catch2::Catch2WithMain OR TARGET Catch2::Catch2)
    set(AIQUANT_HAVE_CATCH TRUE)
else()
    set(AIQUANT_HAVE_CATCH FALSE)
    message(STATUS "Catch2 not found; unit tests will be skipped.")
endif()

# Configure Test Executable
file(GLOB_RECURSE TEST_SOURCES "tests/*.cpp" "tests/*.cc" "tests/*.cxx")
# The TA-Lib golden tests build into their own optional target, not into aiquant_tests.
list(FILTER TEST_SOURCES EXCLUDE REGEX "/tests/golden/")

if(AIQUANT_HAVE_CATCH AND TEST_SOURCES)
    add_executable(aiquant_tests ${TEST_SOURCES})
    target_compile_definitions(aiquant_tests PRIVATE AIQUANT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\")

    if(TARGET Catch2::Catch2WithMain)
        target_link_libraries(aiquant_tests PRIVATE Catch2::Catch2WithMain fin_api fin_stream)
        message(STATUS "Using Catch2 v3")
    else()
        target_link_libraries(aiquant_tests PRIVATE Catch2::Catch2 fin_api fin_stream)
        message(STATUS "Using the bundled minicatch or Catch2 v2")
    endif()

    target_include_directories(aiquant_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_features(aiquant_tests PRIVATE cxx_std_20)
    include(CTest)

    if(TARGET Catch2::Catch2WithMain)
        # include(Catch) has to come first: it is what defines catch_discover_tests.
        # Both Catch2Config.cmake and the FetchContent build put extras/ on CMAKE_MODULE_PATH.
        include(Catch)
        # PRE_TEST keeps discovery out of the build step, which matters under sanitizers.
        catch_discover_tests(aiquant_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS unit)
    else()
        # minicatch's main() ignores argv, so the whole binary is one opaque ctest entry.
        add_test(NAME aiquant_tests COMMAND aiquant_tests)
    endif()
else()
    message(STATUS "Catch2 not found or no test files found. Unit tests disabled.")
endif()

# ============ TA-Lib golden tests (optional) ============
# Off by default: TA-Lib is not installed on every dev machine. CI turns it on.
option(AIQUANT_WITH_TALIB "Build the golden tests that compare indicators against TA-Lib" OFF)

if(AIQUANT_WITH_TALIB)
    # pkg-config is only a hint; TA-Lib ships ta-lib.pc but pkg-config itself may be absent.
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PC_TALIB QUIET ta-lib)
    endif()

    find_path(TALIB_INCLUDE_DIR
        NAMES ta-lib/ta_libc.h
        HINTS ${PC_TALIB_INCLUDE_DIRS} ${PC_TALIB_INCLUDEDIR}
    )
    find_library(TALIB_LIBRARY
        NAMES ta-lib ta_lib
        HINTS ${PC_TALIB_LIBRARY_DIRS} ${PC_TALIB_LIBDIR}
    )

    file(GLOB_RECURSE TALIB_TEST_SOURCES "tests/golden/*.cpp")

    if(NOT TARGET Catch2::Catch2WithMain)
        message(WARNING "AIQUANT_WITH_TALIB=ON needs real Catch2 v3; golden tests skipped")
    elseif(NOT TALIB_INCLUDE_DIR OR NOT TALIB_LIBRARY)
        message(WARNING "AIQUANT_WITH_TALIB=ON but TA-Lib was not found; golden tests skipped")
    elseif(TALIB_TEST_SOURCES)
        add_executable(aiquant_talib_tests ${TALIB_TEST_SOURCES})
        target_include_directories(aiquant_talib_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
            ${TALIB_INCLUDE_DIR}
        )
        target_link_libraries(aiquant_talib_tests PRIVATE fin_indicators Catch2::Catch2WithMain ${TALIB_LIBRARY})
        target_compile_features(aiquant_talib_tests PRIVATE cxx_std_20)
        catch_discover_tests(aiquant_talib_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS golden)
        message(STATUS "TA-Lib golden tests enabled: ${TALIB_LIBRARY}")
    endif()
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
