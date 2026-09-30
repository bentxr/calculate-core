#pragma once

#include <boost/multiprecision/cpp_bin_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

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

// q * 2^e, exactly.
inline Rational scaleByPowerOfTwo(const Rational& q, long long e) {
    if (e == 0) return q;
    const Integer power = Integer(1) << static_cast<unsigned>(e < 0 ? -e : e);
    return e > 0 ? Rational(numerator(q) * power, denominator(q))
                 : Rational(numerator(q), denominator(q) * power);
}

// The exact value of a finite x.
template <class T>
Rational toRational(const T& x) {
    if constexpr (isExact<T>) {
        return x;
    } else if constexpr (std::is_floating_point_v<T>) {
        return Rational(x);
    } else {
        return x.template convert_to<Rational>();
    }
}

namespace impl {

// n (0 <= n <= 2^p) as a T, exactly: built 32 bits at a time from the top, so every partial
// value is an integer no larger than n.
template <class T>
T integerToFloat(const Integer& n) {
    using std::ldexp;
    if (n == 0) return T(0);
    const unsigned bits = static_cast<unsigned>(msb(n)) + 1;
    int shift = static_cast<int>(bits - (bits % 32 == 0 ? 32 : bits % 32));
    T t = T(((n >> shift) & 0xFFFFFFFFu).template convert_to<std::uint32_t>());
    while (shift > 0) {
        shift -= 32;
        t = ldexp(t, 32) + T(((n >> shift) & 0xFFFFFFFFu).template convert_to<std::uint32_t>());
    }
    return t;
}

}  // namespace impl

// q rounded to the nearest T, ties to even. Overflow gives ±infinity. Below the normal range,
// subnormals for types that have them, and a flush to zero (like cpp_bin_float) otherwise.
template <class T>
T fromRational(const Rational& q) {
    if constexpr (isExact<T>) {
        return q;
    } else {
        using std::ldexp;
        if (q == 0) return T(0);
        const bool negative = q < 0;
        const T infinity = std::numeric_limits<T>::infinity();
        const Integer n = abs(numerator(q));
        const Integer d = denominator(q);
        long long e = static_cast<long long>(msb(n)) - static_cast<long long>(msb(d));
        const bool below = e >= 0 ? n < (d << static_cast<unsigned>(e)) : (n << static_cast<unsigned>(-e)) < d;
        if (below) --e;  // now 2^e <= |q| < 2^(e+1)
        const int p = precisionBits<T>();
        const long long emin = minExponent<T>();
        const long long emax = maxExponent<T>();
        if (e > emax) return negative ? T(-infinity) : infinity;
        const long long quantumExp = (hasSubnormals<T>() ? std::max(e, emin) : e) - (p - 1);
        Integer num = n;
        Integer den = d;
        if (quantumExp < 0) num <<= static_cast<unsigned>(-quantumExp);
        else den <<= static_cast<unsigned>(quantumExp);
        Integer rounded = num / den;
        const Integer rest = num - rounded * den;
        if (2 * rest > den || (2 * rest == den && (rounded & 1) != 0)) ++rounded;
        if (rounded == 0) return negative ? T(-T(0)) : T(0);
        const long long top = quantumExp + static_cast<long long>(msb(rounded));
        if (top > emax) return negative ? T(-infinity) : infinity;
        if (!hasSubnormals<T>() && top < emin) return negative ? T(-T(0)) : T(0);
        const T t = ldexp(impl::integerToFloat<T>(rounded), static_cast<int>(quantumExp));
        return negative ? T(-t) : t;
    }
}

template <class To, class From>
To exactCast(const From& x) {
    return fromRational<To>(toRational(x));
}

}  // namespace calculate_core::detail
