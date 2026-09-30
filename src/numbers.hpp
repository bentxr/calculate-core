#pragma once

#include <boost/multiprecision/cpp_bin_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

namespace calculate_core::detail {

namespace mp = boost::multiprecision;

using Integer = mp::number<mp::cpp_int_backend<>, mp::et_off>;
using Rational = mp::number<mp::cpp_rational_backend, mp::et_off>;

// IEEE 754 binary interchange format of k bits (k a power of two, k >= 128):
// exponent field w = round(4 log2 k) - 13, precision p = k - w, emax = 2^(w-1) - 1.
constexpr int ieeeExponentBits(int k) {
    int log2k = 0;
    while (k > 1) {
        k >>= 1;
        ++log2k;
    }
    return 4 * log2k - 13;
}

constexpr int ieeePrecision(int k) { return k - ieeeExponentBits(k); }

constexpr int ieeeMaxExponent(int k) { return (1 << (ieeeExponentBits(k) - 1)) - 1; }

template <int K>
using IeeeBinary = mp::number<mp::cpp_bin_float<ieeePrecision(K), mp::digit_base_2, void, std::int32_t,
                                                1 - ieeeMaxExponent(K), ieeeMaxExponent(K)>,
                              mp::et_off>;

using Binary128 = IeeeBinary<128>;
using Binary256 = IeeeBinary<256>;
using Binary512 = IeeeBinary<512>;
using Ruler = IeeeBinary<1024>;       // error-report arithmetic and the shadow
using RulerCheck = IeeeBinary<2048>;  // agreement check for the shadow

template <class T>
inline constexpr bool isExact = std::numeric_limits<T>::is_exact;

// numeric_limits<T>::digits (inexact T only).
template <class T>
int precisionBits() {
    return std::numeric_limits<T>::digits;
}

// e with min() == 2^e. Derived from min() itself: Boost's numeric_limits::min_exponent
// follows a different convention from the hardware types.
template <class T>
int minExponent() {
    using std::frexp;
    int e;
    frexp((std::numeric_limits<T>::min)(), &e);
    return e - 1;
}

// e with 2^e <= max() < 2^(e+1).
template <class T>
int maxExponent() {
    using std::frexp;
    int e;
    frexp((std::numeric_limits<T>::max)(), &e);
    return e - 1;
}

template <class T>
bool hasSubnormals() {
    return std::numeric_limits<T>::has_denorm == std::denorm_present;
}

// 2^-p, or 0 for exact T.
template <class T>
T unitRoundoff() {
    if constexpr (isExact<T>) {
        return T(0);
    } else {
        using std::ldexp;
        return ldexp(T(1), -precisionBits<T>());
    }
}

template <class T>
bool isFinite(const T& x) {
    if constexpr (isExact<T>) {
        return true;
    } else {
        using std::isfinite;
        return isfinite(x);
    }
}

}  // namespace calculate_core::detail
