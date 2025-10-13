#pragma once
#include <iostream>
#include <vector>
#include <sstream>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <numeric>
#include <queue>
#include <memory>
#include <stdint.h>
#include <assert.h>

//========================= printCollection ===========================

template<class ContainerT>
std::ostream &printCollection(std::ostream &os, const ContainerT &vec, const char *quote = "{}");

template<class... Args>
std::ostream &operator<<(std::ostream &os, const std::vector<Args...> &vec) {
    return printCollection(os, vec, "[]");
}

template<class... Args>
std::ostream &operator<<(std::ostream &os, const std::pair<Args...> &p) {
    os << p.first << " : " << p.second;
    return os;
}

template<class... Args>
std::ostream &operator<<(std::ostream &os, const std::map<Args...> &m) {
    return printCollection(os, m, "{}");
}
template<class... Args>
std::ostream &operator<<(std::ostream &os, const std::unordered_map<Args...> &m) {
    return printCollection(os, m, "{}");
}
template<class... Args>
std::ostream &operator<<(std::ostream &os, const std::set<Args...> &m) {
    return printCollection(os, m, "{}");
}
template<class... Args>
std::ostream &operator<<(std::ostream &os, const std::unordered_set<Args...> &m) {
    return printCollection(os, m, "{}");
}
template<class ContainerT>
std::ostream &printCollection(std::ostream &os, const ContainerT &vec, const char *quote) {
    if (quote && quote[0]) os << quote[0];
    auto i = 0U;
    for (const auto &e : vec) {
        if (i++ != 0) os << ", ";
        os << e;
    }
    if (quote && quote[1]) os << quote[1];
    return os;
}

//========================= macros ===========================

#ifdef ADD_TEST_CASE_AS_FUNCTION
#define ADD_TEST_CASE(name) void name()
#else
#define ADD_TEST_CASE(name) TEST_CASE(#name)
#endif

#define ASSERT(expr)                                                                                                                                \
    do {                                                                                                                                            \
        if (!(expr)) {                                                                                                                              \
            std::cerr << '\n' << __FILE__ << ":" << __LINE__ << ": " << __FUNCTION__ << ": Assertion failed: " #expr << std::endl;                  \
            abort();                                                                                                                                \
        }                                                                                                                                           \
    } while (false)

#define ASSERT_OP0(a, OP, b)                                                                                                                        \
    do {                                                                                                                                            \
        auto _left  = (a);                                                                                                                          \
        auto _right = (b);                                                                                                                          \
        if (!(_left OP _right)) {                                                                                                                   \
            std::cerr << '\n'                                                                                                                       \
                      << __FILE__ << ":" << __LINE__ << ": " << __FUNCTION__ << "\n    Failed assertion: " #a " " #OP " " #b                        \
                      << "\n         Left  value: " << _left << "\n         Right value: " << _right << std::endl;                                  \
            abort();                                                                                                                                \
        }                                                                                                                                           \
    } while (false)

// for c++20
#define ASSERT_OP(a, OP, b)                                                                                                                         \
    std::invoke(                                                                                                                                    \
            [](auto twoValues) {                                                                                                                    \
                if (auto &[_left, _right] = twoValues; !(_left OP _right)) {                                                                        \
                    std::cerr << '\n' << __FILE__ << ":" << __LINE__ << ":\n    Failed assertion: " #a " " #OP " " #b;                              \
                    if constexpr (requires(decltype(twoValues) _ab) { std::cerr << _ab.first << _ab.second; })                                      \
                        std::cerr << "\n         Left  value: " << _left << "\n         Right value: " << _right;                                   \
                    std::cerr << std::endl;                                                                                                         \
                    abort();                                                                                                                        \
                }                                                                                                                                   \
            },                                                                                                                                      \
            std::make_pair((a), (b)))

#define ASSERT_EQ(a, b) ASSERT_OP(a, ==, b)
#define ASSERT_NE(a, b) ASSERT_OP(a, !=, b)
#define ASSERT_LT(a, b) ASSERT_OP(a, <, b)
#define ASSERT_LE(a, b) ASSERT_OP(a, <=, b)


/// Users define USE_CATCH to use CATCH2 test framework. Otherwise, doctest is used as default.
/// TEST_CONFIG_IMPLEMENT_MAIN is used to define main function for the test framwork.

#ifdef USE_CATCH //------------ using catch
#ifdef TEST_CONFIG_IMPLEMENT_MAIN
#define CATCH_CONFIG_MAIN
#endif

#include "catch.hpp"

#define SECTION SUBCASE
#else //---------------- using doctest

#ifdef TEST_CONFIG_IMPLEMENT_MAIN
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#endif

#include "doctest.hpp"

namespace doctest {
template<class... Args>
struct StringMaker<std::vector<Args...>> {
    static String convert(const std::vector<Args...> &value) {
        std::stringstream ss;
        printCollection(ss, value, "[]");
        return ss.str().c_str();
    }
};

template<class... Args>
struct StringMaker<std::map<Args...>> {
    static String convert(const std::map<Args...> &value) {
        std::stringstream ss;
        printCollection(ss, value, "{}");
        return ss.str().c_str();
    }
};
template<class... Args>
struct StringMaker<std::set<Args...>> {
    static String convert(const std::set<Args...> &value) {
        std::stringstream ss;
        printCollection(ss, value, "{}");
        return ss.str().c_str();
    }
};
template<class... Args>
struct StringMaker<std::unordered_map<Args...>> {
    static String convert(const std::unordered_map<Args...> &value) {
        std::stringstream ss;
        printCollection(ss, value, "{}");
        return ss.str().c_str();
    }
};
template<class... Args>
struct StringMaker<std::unordered_set<Args...>> {
    static String convert(const std::unordered_set<Args...> &value) {
        std::stringstream ss;
        printCollection(ss, value, "{}");
        return ss.str().c_str();
    }
};
} // namespace doctest


#endif //------ end ifdef USE_CATH
