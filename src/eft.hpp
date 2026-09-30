#pragma once

#include "numbers.hpp"

#include <cmath>
#include <type_traits>

namespace calculate_core::detail {

// value + error == the exact result of the operation.
template <class T>
struct TwoPart {
    T value;
    T error;
};

// Knuth: exact for any a, b. The operation order is essential.
template <class T>
TwoPart<T> twoSum(const T& a, const T& b) {
    const T s = a + b;
    const T a1 = s - b;
    const T b1 = s - a1;
    const T da = a - a1;
    const T db = b - b1;
    return {s, da + db};
}

// Dekker: exact when |a| >= |b| (or a == 0).
template <class T>
TwoPart<T> fastTwoSum(const T& a, const T& b) {
    const T s = a + b;
    const T z = s - a;
    return {s, b - z};
}

// Hardware types use their fused multiply-add. Boost's fma is not fused, so the other types use
// Dekker's product with Veltkamp's split (1971), which needs |a|, |b| < max() / 2^(s+1).
template <class T>
TwoPart<T> twoProduct(const T& a, const T& b) {
    if constexpr (std::is_floating_point_v<T>) {
        const T p = a * b;
        return {p, std::fma(a, b, -p)};
    } else {
        using std::ldexp;
        const int s = (precisionBits<T>() + 1) / 2;
        const T c = ldexp(T(1), s) + 1;
        const auto split = [&c](const T& x, T& hi, T& lo) {
            const T g = c * x;
            hi = g - (g - x);
            lo = x - hi;
        };
        T ah, al, bh, bl;
        split(a, ah, al);
        split(b, bh, bl);
        const T p = a * b;
        return {p, ((ah * bh - p) + ah * bl + al * bh) + al * bl};
    }
}

}  // namespace calculate_core::detail
