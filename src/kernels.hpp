#pragma once

#include "doubleword.hpp"
#include "numbers.hpp"

#include <array>
#include <cmath>
#include <optional>
#include <type_traits>
#include <vector>

// Elementary functions computed in the selected type T only: exact argument reduction, a series
// in double-word T, one final rounding. No libm or Boost transcendental functions.
namespace calculate_core::detail {

namespace impl {

template <class T>
long long toLongLong(const T& x) {
    if constexpr (std::is_floating_point_v<T>) return static_cast<long long>(x);
    else return x.template convert_to<long long>();
}

// The constants as double words, computed once per type.
template <class T>
const DoubleWord<T>& word(ConstantId id) {
    static const std::array<DoubleWord<T>, 5> words{
        constantWord<T>(ConstantId::Pi), constantWord<T>(ConstantId::TwoOverPi), constantWord<T>(ConstantId::Ln2),
        constantWord<T>(ConstantId::Ln10), constantWord<T>(ConstantId::E)};
    return words[static_cast<std::size_t>(id)];
}

template <class T>
DoubleWord<T> halfPi() {
    return scale(word<T>(ConstantId::Pi), -1);
}

// A series term stops mattering once it is below the sum at twice T's precision.
template <class T>
bool negligible(const DoubleWord<T>& term, const DoubleWord<T>& sum) {
    using std::abs;
    using std::ldexp;
    return abs(term.hi) <= ldexp(abs(sum.hi), -(2 * precisionBits<T>() + 4));
}

// Halvings before a series: the smallest s with 4 s^2 >= p (about sqrt(p)/2).
template <class T>
int halvings() {
    int s = 0;
    while (4 * s * s < precisionBits<T>()) ++s;
    return s;
}

// Largest |k| in x = k ln2 + r that can still give a finite, non-zero result.
template <class T>
long long maxExpMultiple() {
    return static_cast<long long>(maxExponent<T>()) + precisionBits<T>() + 3;
}

inline int bitLength(long long v) {
    int b = 0;
    for (; v; v >>= 1) ++b;
    return b;
}

// ln2 in Cody-Waite pieces: k * piece is exact for every |k| <= maxExpMultiple, and the pieces
// carry about twice T's precision (as far as the table allows).
template <class T>
const std::vector<T>& ln2Pieces() {
    static const std::vector<T> pieces = [] {
        const int kBits = bitLength(maxExpMultiple<T>());
        const int bits = precisionBits<T>() - kBits;
        const int count = (2 * precisionBits<T>() + kBits + 8 + bits - 1) / bits;
        return constantPieces<T>(ConstantId::Ln2, bits, count);
    }();
    return pieces;
}

}  // namespace impl

// expm1 of a small double word, |r| <= 0.35: halve, Taylor series, then undo the halvings with
// expm1(2y) = expm1(y) * (expm1(y) + 2).
template <class T>
DoubleWord<T> expm1Small(const DoubleWord<T>& r) {
    if (r.hi == 0) return r;
    const int s = impl::halvings<T>();
    const DoubleWord<T> a = scale(r, -s);
    DoubleWord<T> term = a;
    DoubleWord<T> sum = a;
    for (int k = 2;; ++k) {
        term = term * a / T(k);
        if (impl::negligible(term, sum)) break;
        sum = sum + term;
    }
    for (int i = 0; i < s; ++i) sum = sum * (sum + T(2));
    return sum;
}

// exp(x) = 2^k (1 + expm1), with x = k ln2 + r.
template <class T>
struct ExpParts {
    DoubleWord<T> expm1;
    long long k = 0;
    bool overflow = false;
    bool underflow = false;
};

template <class T>
ExpParts<T> expParts(const DoubleWord<T>& x) {
    using std::floor;
    ExpParts<T> p;
    const T ln2 = impl::word<T>(ConstantId::Ln2).hi;
    const T limit = T(impl::maxExpMultiple<T>()) * ln2;
    if (x.hi > limit) {
        p.overflow = true;
        return p;
    }
    if (x.hi < -limit) {
        p.underflow = true;
        return p;
    }
    const T kt = floor(x.hi / ln2 + T(0.5));
    p.k = impl::toLongLong(kt);
    DoubleWord<T> r = x;
    for (const T& piece : impl::ln2Pieces<T>()) r = r - T(kt * piece);  // kt * piece is exact
    p.expm1 = expm1Small(r);
    return p;
}

template <class T>
DoubleWord<T> expValue(const ExpParts<T>& p) {
    return scale(p.expm1 + T(1), static_cast<int>(p.k));
}

// log of a positive double word: x = 2^e m, m in [sqrt(1/2), sqrt(2)), log m = 2 atanh((m-1)/(m+1)).
template <class T>
DoubleWord<T> logWord(const DoubleWord<T>& x) {
    using std::frexp;
    int e;
    frexp(x.hi, &e);
    DoubleWord<T> m = scale(x, -e);
    if (m.hi < T(0.70710678118654752)) {  // a reduction boundary, not a precision
        m = scale(m, 1);
        --e;
    }
    const DoubleWord<T> z = (m - T(1)) / (m + T(1));
    DoubleWord<T> sum = z;
    if (z.hi != 0) {
        const DoubleWord<T> z2 = z * z;
        DoubleWord<T> power = z;
        for (int k = 3;; k += 2) {
            power = power * z2;
            const DoubleWord<T> term = power / T(k);
            if (impl::negligible(term, sum)) break;
            sum = sum + term;
        }
    }
    return scale(sum, 1) + impl::word<T>(ConstantId::Ln2) * T(e);
}

// x = n pi/2 + r with |r| <= pi/4, by exact integer arithmetic on the bits of x and of 2/pi
// (Payne-Hanek); the remainder is rounded into T once. Supported for x < 2^1024.
template <class T>
struct Reduced {
    int quadrant = 0;  // n mod 4
    DoubleWord<T> r;
};

template <class T>
std::optional<Reduced<T>> reduceHalfPi(const T& x) {  // x >= 0
    using std::ldexp;
    if (x < T(0.78)) return Reduced<T>{0, dw(x)};  // already within pi/4
    if (x >= ldexp(T(1), 1024)) return std::nullopt;
    static const Integer twoOverPi = constantMantissa(ConstantId::TwoOverPi);
    const Rational q = toRational(x);  // N / 2^d
    const unsigned fraction = constantFractionBits + static_cast<unsigned>(msb(denominator(q)));
    const Integer product = numerator(q) * twoOverPi;  // x * 2/pi * 2^fraction
    const Integer n = ((product >> (fraction - 1)) + 1) >> 1;  // nearest integer to x * 2/pi
    const Integer rest = product - (n << fraction);
    const Rational f = scaleByPowerOfTwo(Rational(rest), -static_cast<long long>(fraction));
    const T fh = fromRational<T>(f);
    const DoubleWord<T> fw{fh, fromRational<T>(f - toRational(fh))};
    return Reduced<T>{static_cast<int>((n & 3).template convert_to<unsigned>()), fw * impl::halfPi<T>()};
}

}  // namespace calculate_core::detail
