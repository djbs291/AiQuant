#pragma once
#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace minicatch {
struct Approx {
    double value;
    double eps;
    explicit Approx(double v) : value(v), eps(1e-12) {}
    Approx &margin(double e) {
        eps = e;
        return *this;
    }
};

inline bool operator==(double lhs, const Approx &rhs) {
    return std::fabs(lhs - rhs.value) <= rhs.eps;
}
inline bool operator==(const Approx &lhs, double rhs) {
    return rhs == lhs;
}
inline bool operator!=(double lhs, const Approx &rhs) {
    return !(lhs == rhs);
}
inline bool operator!=(const Approx &lhs, double rhs) {
    return !(lhs == rhs);
}

struct TestRegistry {
    using Func = std::function<void()>;
    std::vector<std::pair<std::string, Func>> tests;
    static TestRegistry &instance() {
        static TestRegistry inst;
        return inst;
    }
    void add(std::string name, Func f) {
        tests.emplace_back(std::move(name), std::move(f));
    }
    int run() {
        int failed = 0;
        for (auto &t : tests) {
            try {
                t.second();
            } catch (const std::exception &e) {
                std::cerr << t.first << " failed: " << e.what() << "\n";
                ++failed;
            } catch (...) {
                std::cerr << t.first << " failed: unknown exception\n";
                ++failed;
            }
        }
        std::cout << tests.size() - failed << " test(s) passed" << std::endl;
        if (failed)
            std::cout << failed << " test(s) failed" << std::endl;
        return failed;
    }
};

} // namespace minicatch

#define MC_CONCAT_INNER(a, b) a##b
#define MC_CONCAT(a, b) MC_CONCAT_INNER(a, b)

#define TEST_CASE(name, ...)                                                      \
    static void MC_CONCAT(test_func_, __LINE__)();                                \
    namespace {                                                                   \
    struct MC_CONCAT(TestRegistrar_, __LINE__) {                                  \
        MC_CONCAT(TestRegistrar_, __LINE__)() {                                   \
            minicatch::TestRegistry::instance().add(name,                         \
                                                    MC_CONCAT(test_func_,        \
                                                              __LINE__));        \
        }                                                                         \
    } MC_CONCAT(test_registrar_, __LINE__);                                       \
    }                                                                             \
    static void MC_CONCAT(test_func_, __LINE__)()

#define REQUIRE(expr)                                                             \
    do {                                                                          \
        if (!(expr))                                                              \
            throw std::runtime_error("REQUIRE failed: " #expr);                  \
    } while (0)

#define Approx minicatch::Approx

#define REQUIRE_FALSE(expr) REQUIRE(!(expr))

#ifdef CATCH_CONFIG_MAIN
int main() { return minicatch::TestRegistry::instance().run(); }
#endif

