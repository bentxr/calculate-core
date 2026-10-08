#pragma once

#include "constants.hpp"

#include <boost/multiprecision/cpp_bin_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

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

// Exact numbers beyond 10^±1000000 take minutes and gigabytes to compute and write down.
inline constexpr long long exactDigitsLimit = 1000000;

// q * 2^e, exactly.
inline Rational scaleByPowerOfTwo(const Rational& q, long long e) {
    if (e == 0) return q;
    const Integer power = Integer(1) << static_cast<unsigned>(e < 0 ? -e : e);
    return e > 0 ? Rational(numerator(q) * power, denominator(q))
                 : Rational(numerator(q), denominator(q) * power);
}

// The largest integer <= q. (Integer division truncates towards zero; the denominator is >= 1.)
inline Integer floorOf(const Rational& q) {
    Integer n = numerator(q) / denominator(q);
    if (n * denominator(q) > numerator(q)) --n;
    return n;
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

// value = significand * 10^exponent10 (non-negative; unary minus belongs to the grammar).
struct DecimalLiteral {
    Integer significand;
    long long exponent10 = 0;
};

// digits [. [digits]] [exponent] | . digits [exponent], exponent = (e|E) [+|-|−] digits.
// The exponent saturates at ±10^15, so it never overflows.
inline std::optional<DecimalLiteral> parseDecimal(std::string_view text) {
    constexpr long long limit = 1000000000000000LL;
    const auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
    DecimalLiteral d;
    std::size_t i = 0;
    int digits = 0;
    long long fractionDigits = 0;
    for (; i < text.size() && isDigit(text[i]); ++i, ++digits) d.significand = d.significand * 10 + (text[i] - '0');
    if (i < text.size() && text[i] == '.') {
        for (++i; i < text.size() && isDigit(text[i]); ++i, ++digits, ++fractionDigits)
            d.significand = d.significand * 10 + (text[i] - '0');
    }
    if (digits == 0) return std::nullopt;
    long long exponent = 0;
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        bool negative = false;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            negative = text[i++] == '-';
        } else if (text.substr(i, 3) == "\xE2\x88\x92") {  // −, the calculator's minus
            negative = true;
            i += 3;
        }
        const std::size_t start = i;
        for (; i < text.size() && isDigit(text[i]); ++i)
            if (exponent <= limit) exponent = exponent * 10 + (text[i] - '0');
        if (i == start) return std::nullopt;
        exponent = std::min(exponent, limit);
        if (negative) exponent = -exponent;
    }
    if (i != text.size()) return std::nullopt;
    d.exponent10 = std::clamp(exponent - fractionDigits, -limit, limit);
    return d;
}

// Exact. Precondition: |exponent10| <= 10^6 (a larger literal cannot be materialized).
inline Rational toRational(const DecimalLiteral& d) {
    const long long e = d.exponent10;
    const Integer power = pow(Integer(10), static_cast<unsigned>(e < 0 ? -e : e));
    return e < 0 ? Rational(d.significand, power) : Rational(d.significand * power);
}

namespace impl {

// Conservative decimal range of T: beyond these, a literal certainly overflows or rounds to 0.
// (30103/100000 approximates log10(2); the margins cover the error.)
template <class T>
long long maxDecimalExponent() {
    return (static_cast<long long>(maxExponent<T>()) + 1) * 30103 / 100000 + 2;
}

template <class T>
long long minDecimalExponent() {
    const long long smallest = hasSubnormals<T>() ? minExponent<T>() - precisionBits<T>() + 1 : minExponent<T>();
    return (smallest - 1) * 30103 / 100000 - 3;
}

}  // namespace impl

// The literal correctly rounded into T (exact for Rational).
template <class T>
T decimalTo(const DecimalLiteral& d) {
    if constexpr (isExact<T>) {
        return toRational(d);
    } else {
        if (d.significand == 0) return T(0);
        const long long e10 = d.exponent10 + static_cast<long long>(d.significand.str().size()) - 1;
        if (e10 > impl::maxDecimalExponent<T>()) return std::numeric_limits<T>::infinity();
        if (e10 < impl::minDecimalExponent<T>()) return T(0);
        return fromRational<T>(toRational(d));
    }
}

// value = (negative ? -1 : 1) * d1.d2d3... * 10^exponent10; no trailing zeros; zero is {"0", 0}.
struct DecimalDigits {
    bool negative = false;
    std::string digits;
    long long exponent10 = 0;
};

// Every decimal digit of a finite binary value: N / 2^k = (N * 5^k) / 10^k.
template <class T>
DecimalDigits exactDigits(const T& x) {
    const Rational q = toRational(x);
    if (q == 0) return {false, "0", 0};
    const Integer n = abs(numerator(q));
    const unsigned k = static_cast<unsigned>(msb(denominator(q)));  // the denominator is 2^k
    std::string s = (k > 0 ? Integer(n * pow(Integer(5), k)) : n).str();
    DecimalDigits d;
    d.negative = q < 0;
    d.exponent10 = static_cast<long long>(s.size()) - 1 - k;
    s.erase(s.find_last_not_of('0') + 1);
    d.digits = std::move(s);
    return d;
}

// Sign separate; numerator/denominator reduced, denominator >= 1. When hasDecimal:
// value = integerPart . fractionDigits (repeatingDigits repeated forever).
struct FractionDigits {
    bool negative = false;
    std::string numerator;
    std::string denominator;
    bool hasDecimal = false;
    std::string integerPart;
    std::string fractionDigits;
    std::string repeatingDigits;
};

// Long division: the factors 2 and 5 of the denominator give the digits before the period;
// the period ends when the remainder comes back. Periods longer than maxPeriod are not shown.
inline FractionDigits exactFraction(const Rational& q, int maxPeriod = 60) {
    FractionDigits f;
    f.negative = q < 0;
    const Integer n = abs(numerator(q));
    const Integer d = denominator(q);  // >= 1
    f.numerator = n.str();
    f.denominator = d.str();
    Integer rest = d;
    int twos = 0;
    int fives = 0;
    for (; rest % 2 == 0; rest /= 2) ++twos;
    for (; rest % 5 == 0; rest /= 5) ++fives;
    const auto nextDigit = [&d](Integer& r) {
        r *= 10;
        const Integer digit = r / d;
        r %= d;
        return digit.str();
    };
    f.integerPart = Integer(n / d).str();
    Integer r = n % d;
    for (int i = 0; i < std::max(twos, fives); ++i) f.fractionDigits += nextDigit(r);
    if (r == 0) {
        f.hasDecimal = true;
        return f;
    }
    const Integer start = r;
    for (int i = 0; i < maxPeriod; ++i) {
        f.repeatingDigits += nextDigit(r);
        if (r == start) {
            f.hasDecimal = true;
            return f;
        }
    }
    f.integerPart.clear();
    f.fractionDigits.clear();
    f.repeatingDigits.clear();
    return f;
}

enum class ConstantId { Pi, TwoOverPi, Ln2, Ln10, E, Sqrt2, Phi, Plastic, EulerGamma };

// The table's hex digits as an integer: the constant times 2^constantFractionBits, truncated.
inline Integer constantMantissa(ConstantId id) {
    const char* hex = nullptr;
    switch (id) {
    case ConstantId::Pi: hex = piHex; break;
    case ConstantId::TwoOverPi: hex = twoOverPiHex; break;
    case ConstantId::Ln2: hex = ln2Hex; break;
    case ConstantId::Ln10: hex = ln10Hex; break;
    case ConstantId::E: hex = eHex; break;
    case ConstantId::Sqrt2: hex = sqrt2Hex; break;
    case ConstantId::Phi: hex = phiHex; break;
    case ConstantId::Plastic: hex = plasticHex; break;
    case ConstantId::EulerGamma: hex = egammaHex; break;
    }
    Integer m = 0;
    for (const char* c = hex; *c; ++c) m = m * 16 + (*c <= '9' ? *c - '0' : *c - 'A' + 10);
    return m;
}

// The table value exactly.
inline Rational constantRational(ConstantId id) {
    return scaleByPowerOfTwo(Rational(constantMantissa(id)), -constantFractionBits);
}

// The constant correctly rounded into T.
template <class T>
T constantValue(ConstantId id) {
    return fromRational<T>(constantRational(id));
}

}  // namespace calculate_core::detail
