#pragma once

#if __has_include(<catch2/catch_test_macros.hpp>)
// Real Catch2 v3.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
// v3 dropped the global Approx; this keeps the unqualified Approx(...) in the tests working.
using Catch::Approx;
#else
// Catch2 v2, or the bundled minicatch that mimics its header layout.
#include <catch2/catch.hpp>
#endif
