#pragma once

#include "eft.hpp"
#include "numbers.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace calculate_core::detail {

// An unevaluated sum hi + lo of two T with |lo| <= ulp(hi)/2: about twice T's precision.
// Algorithms from Joldes, Muller & Popescu, "Tight and rigorous error bounds for basic building
// blocks of double-word arithmetic" (2017). Their operation order is part of their correctness.
template <class T>
struct DoubleWord {
    T hi = 0;
    T lo = 0;
};

template <class T>
DoubleWord<T> dw(const T& x) {
    return {x, T(0)};
}

// hi + lo, rounded once.
template <class T>
T toValue(const DoubleWord<T>& x) {
    return x.hi + x.lo;
}

template <class T>
DoubleWord<T> operator-(const DoubleWord<T>& x) {
    return {-x.hi, -x.lo};
}

// x * 2^e, exactly.
template <class T>
DoubleWord<T> scale(const DoubleWord<T>& x, int e) {
    using std::ldexp;
    return {ldexp(x.hi, e), ldexp(x.lo, e)};
}

// Algorithm 6 (AccurateDWPlusDW).
template <class T>
DoubleWord<T> operator+(const DoubleWord<T>& x, const DoubleWord<T>& y) {
    const TwoPart<T> s = twoSum(x.hi, y.hi);
    const TwoPart<T> t = twoSum(x.lo, y.lo);
    const TwoPart<T> v = fastTwoSum(s.value, s.error + t.value);
    const TwoPart<T> z = fastTwoSum(v.value, t.error + v.error);
    return {z.value, z.error};
}

template <class T>
DoubleWord<T> operator-(const DoubleWord<T>& x, const DoubleWord<T>& y) {
    return x + (-y);
}

// Algorithm 4 (DWPlusFP).
template <class T>
DoubleWord<T> operator+(const DoubleWord<T>& x, const T& y) {
    const TwoPart<T> s = twoSum(x.hi, y);
    const TwoPart<T> z = fastTwoSum(s.value, x.lo + s.error);
    return {z.value, z.error};
}

template <class T>
DoubleWord<T> operator-(const DoubleWord<T>& x, const T& y) {
    return x + T(-y);
}

// Algorithm 7 (DWTimesFP1).
template <class T>
DoubleWord<T> operator*(const DoubleWord<T>& x, const T& y) {
    const TwoPart<T> c = twoProduct(x.hi, y);
    const TwoPart<T> t = fastTwoSum(c.value, x.lo * y);
    const TwoPart<T> z = fastTwoSum(t.value, t.error + c.error);
    return {z.value, z.error};
}

// Algorithm 10 (DWTimesDW1).
template <class T>
DoubleWord<T> operator*(const DoubleWord<T>& x, const DoubleWord<T>& y) {
    const TwoPart<T> c = twoProduct(x.hi, y.hi);
    const T cross = x.hi * y.lo + x.lo * y.hi;
    const TwoPart<T> z = fastTwoSum(c.value, c.error + cross);
    return {z.value, z.error};
}

// The T quotient, corrected once by its exact remainder.
template <class T>
DoubleWord<T> operator/(const DoubleWord<T>& x, const DoubleWord<T>& y) {
    const T q = x.hi / y.hi;
    const DoubleWord<T> r = x - y * q;
    const TwoPart<T> z = fastTwoSum(q, r.hi / y.hi);
    return {z.value, z.error};
}

template <class T>
DoubleWord<T> operator/(const DoubleWord<T>& x, const T& y) {
    return x / dw(y);
}

// The T square root, corrected once by its exact residual.
template <class T>
DoubleWord<T> sqrt(const DoubleWord<T>& x) {
    using std::sqrt;
    if (x.hi <= 0) return {T(0), T(0)};
    const T s = sqrt(x.hi);
    const TwoPart<T> p = twoProduct(s, s);
    const DoubleWord<T> r = x - DoubleWord<T>{p.value, p.error};
    const TwoPart<T> z = fastTwoSum(s, r.hi / (T(2) * s));
    return {z.value, z.error};
}

template <class T>
bool operator<(const DoubleWord<T>& x, const T& y) {
    return x.hi < y || (x.hi == y && x.lo < 0);
}

// A constant as a double word, correctly rounded in each part.
template <class T>
DoubleWord<T> constantWord(ConstantId id) {
    const Rational c = constantRational(id);
    const T hi = fromRational<T>(c);
    return {hi, fromRational<T>(c - toRational(hi))};
}

// A constant split into `count` pieces of at most `bits` significant bits each (Cody-Waite):
// k * piece is exact whenever |k| < 2^(p - bits). The pieces sum to the table value, truncated.
template <class T>
std::vector<T> constantPieces(ConstantId id, int bits, int count) {
    const Integer m = constantMantissa(id);
    const long long top = static_cast<long long>(msb(m));
    std::vector<T> pieces;
    for (int j = 0; j < count; ++j) {
        const long long high = top - static_cast<long long>(j) * bits;  // highest bit of piece j
        if (high < 0) break;
        const long long from = std::max(high - bits + 1, 0LL);
        const unsigned width = static_cast<unsigned>(high - from + 1);
        const Integer shifted = m >> static_cast<unsigned>(from);
        const Integer chunk = shifted - ((shifted >> width) << width);  // the low `width` bits
        pieces.push_back(fromRational<T>(scaleByPowerOfTwo(Rational(chunk), from - constantFractionBits)));
    }
    return pieces;
}

}  // namespace calculate_core::detail
