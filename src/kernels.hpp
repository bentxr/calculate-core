#pragma once

#include "doubleword.hpp"
#include "numbers.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <optional>
#include <utility>
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

template <class T>
bool isInteger(const T& x) {
    if constexpr (isExact<T>) {
        return denominator(x) == 1;
    } else {
        using std::trunc;
        return trunc(x) == x;
    }
}

// x must be an integer of an inexact type (the kernels' only use). Values of at least 2^p are all even.
template <class T>
bool isOdd(const T& x) {
    using std::abs;
    using std::ldexp;
    using std::trunc;
    if (abs(x) >= ldexp(T(1), precisionBits<T>())) return false;
    return trunc(x / 2) * 2 != x;
}

inline bool cancelled(const std::atomic<bool>* cancel) {
    return cancel && cancel->load(std::memory_order_relaxed);
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

// Taylor series for |r| <= pi/4.
template <class T>
DoubleWord<T> sinSmall(const DoubleWord<T>& r) {
    const DoubleWord<T> r2 = r * r;
    DoubleWord<T> term = r;
    DoubleWord<T> sum = r;
    for (int k = 1;; ++k) {
        term = -(term * r2 / T((2 * k) * (2 * k + 1)));
        if (impl::negligible(term, sum)) break;
        sum = sum + term;
    }
    return sum;
}

template <class T>
DoubleWord<T> cosSmall(const DoubleWord<T>& r) {
    const DoubleWord<T> r2 = r * r;
    DoubleWord<T> term = dw(T(1));
    DoubleWord<T> sum = dw(T(1));
    for (int k = 1;; ++k) {
        term = -(term * r2 / T((2 * k - 1) * (2 * k)));
        if (impl::negligible(term, sum)) break;
        sum = sum + term;
    }
    return sum;
}

// sin x and cos x as double words, by exact reduction (nullopt when |x| >= 2^1024).
template <class T>
std::optional<std::pair<DoubleWord<T>, DoubleWord<T>>> sinCosWord(const T& x) {
    using std::abs;
    const auto reduced = reduceHalfPi(abs(x));
    if (!reduced) return std::nullopt;
    const int q = reduced->quadrant;
    const DoubleWord<T> s = sinSmall(reduced->r);
    const DoubleWord<T> c = cosSmall(reduced->r);
    const DoubleWord<T> sine = q == 0 ? s : q == 1 ? c : q == 2 ? -s : -c;
    const DoubleWord<T> cosine = q == 0 ? c : q == 1 ? -s : q == 2 ? -c : s;
    return std::make_pair(x < 0 ? -sine : sine, cosine);
}

// sinh or cosh of 0 <= ax <= (p + 2) ln2 / 2 as a double word (beyond that, callers use e^ax / 2 directly).
template <class T>
DoubleWord<T> sinhCoshWord(const T& ax, bool sinh) {
    if (ax < 1) {
        DoubleWord<T> m = expm1Small(dw(ax / 4));  // expm1(|x|) by two doublings
        m = m * (m + T(2));
        m = m * (m + T(2));
        if (sinh) return scale(m + m / (m + T(1)), -1);
        return scale(m * m / (m + T(1)), -1) + T(1);
    }
    const DoubleWord<T> y = expValue(expParts(dw(ax)));
    const DoubleWord<T> inverse = dw(T(1)) / y;
    return scale(sinh ? y - inverse : y + inverse, -1);
}

// atan by argument halving, atan(a) = 2 atan(a / (1 + sqrt(1 + a^2))), then its Taylor series.
template <class T>
DoubleWord<T> atanWord(DoubleWord<T> a) {
    using std::ldexp;
    const bool negative = a.hi < 0;
    if (negative) a = -a;
    // Beyond 2^p, atan(a) = pi/2 - 1/a to below 2^-2p (the next term is 1/(3a^3)); and 1/a as a double word
    // would split a, which overflows near T's maximum.
    if (a.hi > ldexp(T(1), precisionBits<T>())) {
        const DoubleWord<T> r = impl::halfPi<T>() - dw(T(1) / a.hi);
        return negative ? -r : r;
    }
    const bool inverted = a.hi > 1;
    if (inverted) a = dw(T(1)) / a;
    const int s = impl::halvings<T>();
    for (int i = 0; i < s; ++i) a = a / (sqrt(a * a + T(1)) + T(1));
    DoubleWord<T> sum = a;
    if (a.hi != 0) {
        const DoubleWord<T> a2 = a * a;
        DoubleWord<T> power = a;
        // After the halvings each term is at least 4 times smaller than the one before, so p terms suffice; the
        // limit also ends the loop on a NaN, which is never negligible.
        for (int k = 3; k < 4 * precisionBits<T>(); k += 2) {
            power = -(power * a2);
            const DoubleWord<T> term = power / T(k);
            if (impl::negligible(term, sum)) break;
            sum = sum + term;
        }
    }
    sum = scale(sum, s);
    if (inverted) sum = impl::halfPi<T>() - sum;
    return negative ? -sum : sum;
}

// asin for 0 <= a <= 1/2, where 1 - a^2 has no cancellation.
template <class T>
DoubleWord<T> asinSmall(const DoubleWord<T>& a) {
    return atanWord(a / sqrt((dw(T(1)) - a) * (a + T(1))));
}

// For |x| > 1/2: asin(x) = pi/2 - 2 asin(sqrt((1 - x)/2)).
template <class T>
DoubleWord<T> asinWord(const DoubleWord<T>& x) {
    const bool negative = x.hi < 0;
    const DoubleWord<T> a = negative ? -x : x;
    const DoubleWord<T> r = a.hi <= T(0.5) ? asinSmall(a)
                                           : impl::halfPi<T>() - scale(asinSmall(sqrt(scale(dw(T(1)) - a, -1))), 1);
    return negative ? -r : r;
}

template <class T>
DoubleWord<T> acosWord(const DoubleWord<T>& x) {
    if (x.hi > T(0.5)) return scale(asinSmall(sqrt(scale(dw(T(1)) - x, -1))), 1);
    if (x.hi < T(-0.5)) return impl::word<T>(ConstantId::Pi) - scale(asinSmall(sqrt(scale(x + T(1), -1))), 1);
    return impl::halfPi<T>() - asinWord(x);
}

}  // namespace calculate_core::detail
