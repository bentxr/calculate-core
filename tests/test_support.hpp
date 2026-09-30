#pragma once

#include "numbers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <type_traits>

namespace test {

using namespace calculate_core::detail;

using FloatingTypes = ::testing::Types<float, double, long double, Binary128, Binary256, Binary512>;
using AllTypes = ::testing::Types<float, double, long double, Rational, Binary128, Binary256, Binary512>;

struct TypeNames {
    template <class T>
    static std::string GetName(int) {
        if constexpr (std::is_same_v<T, float>) return "float";
        else if constexpr (std::is_same_v<T, double>) return "double";
        else if constexpr (std::is_same_v<T, long double>) return "longDouble";
        else if constexpr (std::is_same_v<T, Rational>) return "Rational";
        else if constexpr (std::is_same_v<T, Binary128>) return "Binary128";
        else if constexpr (std::is_same_v<T, Binary256>) return "Binary256";
        else if constexpr (std::is_same_v<T, Binary512>) return "Binary512";
        else if constexpr (std::is_same_v<T, Ruler>) return "Ruler";
        else return "RulerCheck";
    }
};

// A random normal number: p random significand bits, binary exponent in [-spread, spread].
template <class T>
T randomFinite(std::mt19937_64& rng, int spread = 60) {
    using std::ldexp;
    spread = std::min(spread, maxExponent<T>() - 1);  // stay inside the type's normal range
    const int p = precisionBits<T>();
    T m = 1;  // the leading significand bit
    for (int filled = 1; filled < p;) {
        const int n = std::min(32, p - filled);
        m = ldexp(m, n) + T(static_cast<std::uint32_t>(rng() >> (64 - n)));
        filled += n;
    }
    std::uniform_int_distribution<int> exponent(-spread, spread);
    const T x = ldexp(m, exponent(rng) - (p - 1));
    return (rng() & 1) ? T(-x) : x;
}

}  // namespace test
