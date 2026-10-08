#pragma once

#include "numbers.hpp"

#include <calculate-core/calculate-core.hpp>

#include <string_view>
#include <type_traits>

// IEEE 754 binary formats, their encodings and neighbours, computed exactly from the value (never from memory).
namespace calculate_core::detail {

// 1 sign bit, w exponent bits, t significand bits.
struct BinaryFormat {
    int exponentBits;         // w
    int fractionBits;         // t (x87: 64, its leading bit included)
    bool explicitLeadingBit;  // x87 stores the leading significand bit; IEEE 754 implies it
    constexpr int storageBits() const { return 1 + exponentBits + fractionBits; }
    constexpr int precision() const { return explicitLeadingBit ? fractionBits : fractionBits + 1; }
    constexpr int bias() const { return (1 << (exponentBits - 1)) - 1; }
    constexpr int minExponent() const { return 1 - bias(); }
    constexpr int maxExponent() const { return bias(); }
};

// IEEE 754-2019 Table 3.5 for 16/32/64; the interchange formula for 128 and above; bfloat16 is binary32 cut to 16
// bits; x87 is Intel's 80-bit extended format.
inline constexpr BinaryFormat binary16{5, 10, false};
inline constexpr BinaryFormat bfloat16{8, 7, false};
inline constexpr BinaryFormat binary32{8, 23, false};
inline constexpr BinaryFormat binary64{11, 52, false};
inline constexpr BinaryFormat x87Extended{15, 64, true};
inline constexpr BinaryFormat binary128{ieeeExponentBits(128), ieeePrecision(128) - 1, false};
inline constexpr BinaryFormat binary256{ieeeExponentBits(256), ieeePrecision(256) - 1, false};
inline constexpr BinaryFormat binary512{ieeeExponentBits(512), ieeePrecision(512) - 1, false};

// The format a type stores its values in: one rule for float, double, the browser's long double and every Boost type.
template <class T>
BinaryFormat formatOf() {
    if constexpr (std::is_same_v<T, long double>)
        if (precisionBits<T>() == 64) return x87Extended;
    int w = 1;  // 1 + log2(emax + 1)
    for (long long n = static_cast<long long>(maxExponent<T>()) + 1; n > 1; n /= 2) ++w;
    return {w, precisionBits<T>() - 1, false};
}

}  // namespace calculate_core::detail
