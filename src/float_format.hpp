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

// One datum of a format: its class and sign, and its exact magnitude (finite classes) or NaN payload.
struct FloatValue {
    calculate_core::FloatClass kind = calculate_core::FloatClass::Zero;
    bool negative = false;
    Rational magnitude;     // Zero, Subnormal, Normal, and the x87 pseudo-denormal; 0 otherwise
    Integer payload;        // NaNs: the significand field below the quiet bit
    std::string_view note;  // Noncanonical: "pseudo-denormal", "unnormal", "pseudo-infinity" or "pseudo-NaN"
};

// magnitude >= 0, with that sign, rounded to nearest-even into f (subnormals: whether f keeps them).
inline FloatValue roundToFormat(bool negative, const Rational& magnitude, const BinaryFormat& f, bool subnormals) {
    using calculate_core::FloatClass;
    FloatValue v;
    v.negative = negative;
    const impl::Rounded r = impl::roundBinary(magnitude, f.precision(), f.minExponent(), f.maxExponent(), subnormals);
    if (r.overflow) {
        v.kind = FloatClass::Infinite;
        return v;
    }
    if (r.significand == 0) return v;
    v.magnitude = scaleByPowerOfTwo(Rational(r.significand), r.exponent);
    v.kind = v.magnitude >= scaleByPowerOfTwo(Rational(1), f.minExponent()) ? FloatClass::Normal : FloatClass::Subnormal;
    return v;
}

// The bit pattern of v in f, as an integer. The fields as IEEE 754 §3.4 lays them out (x87: Intel SDM vol. 1 §4.2.2).
// v is not Noncanonical, and a finite magnitude is exactly representable in f.
inline Integer encode(const BinaryFormat& f, const FloatValue& v) {
    using calculate_core::FloatClass;
    const unsigned w = static_cast<unsigned>(f.exponentBits);
    const unsigned t = static_cast<unsigned>(f.fractionBits);
    const int p = f.precision();
    const long long emin = f.minExponent();
    const Integer one = 1;
    const Integer allOnes = (one << w) - 1;
    Integer exponent = 0, fraction = 0;
    switch (v.kind) {
    case FloatClass::Zero: break;
    case FloatClass::Subnormal:
    case FloatClass::Normal: {
        const long long e = impl::floorLog2(v.magnitude);
        if (e >= emin) {
            exponent = Integer(e + f.bias());
            const Integer m = numerator(scaleByPowerOfTwo(v.magnitude, p - 1 - e));
            fraction = f.explicitLeadingBit ? m : Integer(m - (one << static_cast<unsigned>(p - 1)));
        } else {  // below the normal range: no leading bit, also for the x87
            fraction = numerator(scaleByPowerOfTwo(v.magnitude, p - 1 - emin));
        }
        break;
    }
    case FloatClass::Infinite:
        exponent = allOnes;
        if (f.explicitLeadingBit) fraction = one << (t - 1);
        break;
    case FloatClass::QuietNaN:
    case FloatClass::SignalingNaN: {
        exponent = allOnes;
        const Integer quiet = f.explicitLeadingBit ? Integer(one << (t - 2)) : Integer(one << (t - 1));
        fraction = v.payload;
        if (f.explicitLeadingBit) fraction |= one << (t - 1);
        if (v.kind == FloatClass::QuietNaN) fraction |= quiet;
        break;
    }
    case FloatClass::Noncanonical: break;  // never encoded: see the precondition
    }
    const Integer sign = v.negative ? Integer(1) : Integer(0);
    return (sign << static_cast<unsigned>(f.storageBits() - 1)) | (exponent << t) | fraction;
}

// The datum a bit pattern of f stands for (0 <= bits < 2^storageBits), the x87's non-canonical patterns included.
inline FloatValue decode(const BinaryFormat& f, const Integer& bits) {
    using calculate_core::FloatClass;
    const unsigned w = static_cast<unsigned>(f.exponentBits);
    const unsigned t = static_cast<unsigned>(f.fractionBits);
    const int p = f.precision();
    const long long emin = f.minExponent();
    const Integer one = 1;
    const Integer allOnes = (one << w) - 1;
    const Integer exponent = (bits >> t) & allOnes;
    const Integer fraction = bits & ((one << t) - 1);
    FloatValue v;
    v.negative = (bits >> static_cast<unsigned>(f.storageBits() - 1)) != 0;
    const auto scaled = [](const Integer& m, long long e) { return scaleByPowerOfTwo(Rational(m), e); };
    if (!f.explicitLeadingBit) {
        if (exponent == 0) {
            if (fraction == 0) return v;
            v.kind = FloatClass::Subnormal;
            v.magnitude = scaled(fraction, emin - (p - 1));
        } else if (exponent == allOnes) {
            if (fraction == 0) {
                v.kind = FloatClass::Infinite;
            } else {
                const Integer quiet = one << (t - 1);
                v.kind = (fraction & quiet) != 0 ? FloatClass::QuietNaN : FloatClass::SignalingNaN;
                v.payload = fraction & (quiet - 1);
            }
        } else {
            v.kind = FloatClass::Normal;
            v.magnitude = scaled((one << static_cast<unsigned>(p - 1)) + fraction, static_cast<long long>(exponent) - f.bias() - (p - 1));
        }
        return v;
    }
    // x87: the leading bit J is stored.
    const bool leading = (fraction >> (t - 1)) != 0;
    const Integer low = fraction & ((one << (t - 1)) - 1);
    if (exponent == 0) {
        if (fraction == 0) return v;
        v.kind = leading ? FloatClass::Noncanonical : FloatClass::Subnormal;
        if (leading) v.note = "pseudo-denormal";
        v.magnitude = scaled(fraction, emin - (p - 1));
    } else if (exponent == allOnes) {
        if (!leading) {
            v.kind = FloatClass::Noncanonical;
            v.note = low == 0 ? "pseudo-infinity" : "pseudo-NaN";
        } else if (low == 0) {
            v.kind = FloatClass::Infinite;
        } else {
            const Integer quiet = one << (t - 2);
            v.kind = (low & quiet) != 0 ? FloatClass::QuietNaN : FloatClass::SignalingNaN;
            v.payload = low & (quiet - 1);
        }
    } else if (!leading) {
        v.kind = FloatClass::Noncanonical;
        v.note = "unnormal";
    } else {
        v.kind = FloatClass::Normal;
        v.magnitude = scaled(fraction, static_cast<long long>(exponent) - f.bias() - (p - 1));
    }
    return v;
}

// The datum a value of an inexact type T holds, from the value itself (never its memory).
template <class T>
FloatValue valueOf(const T& x) {
    using calculate_core::FloatClass;
    using std::isinf;
    using std::isnan;
    using std::signbit;
    FloatValue v;
    v.negative = signbit(x);
    if (isnan(x)) {  // every NaN reads as quiet: the engine never makes one, and its payload would need the memory
        v.kind = FloatClass::QuietNaN;
        return v;
    }
    if (isinf(x)) {
        v.kind = FloatClass::Infinite;
        return v;
    }
    v.magnitude = abs(toRational(x));
    if (v.magnitude == 0) return v;
    v.kind = v.magnitude < scaleByPowerOfTwo(Rational(1), minExponent<T>()) ? FloatClass::Subnormal : FloatClass::Normal;
    return v;
}

// The exponent of the spacing between v and the next value away from zero (v finite): ulp = 2^ulpExponent.
inline long long ulpExponent(const BinaryFormat& f, bool subnormals, const FloatValue& v) {
    const long long p = f.precision();
    const long long emin = f.minExponent();
    if (v.kind == calculate_core::FloatClass::Zero) return subnormals ? emin - p + 1 : emin;
    return std::max(impl::floorLog2(v.magnitude), emin) - (p - 1);
}

namespace impl {

// The largest finite magnitude of f, and the smallest positive one.
inline Rational largest(const BinaryFormat& f) {
    const Integer one = 1;
    const long long p = f.precision();
    return scaleByPowerOfTwo(Rational((one << static_cast<unsigned>(p)) - 1), f.maxExponent() - p + 1);
}
inline Rational smallest(const BinaryFormat& f, bool subnormals) {
    return scaleByPowerOfTwo(Rational(1), subnormals ? f.minExponent() - f.precision() + 1 : f.minExponent());
}

// A finite datum with this sign and magnitude, classed by its magnitude.
inline FloatValue finiteValue(const BinaryFormat& f, bool negative, const Rational& magnitude) {
    using calculate_core::FloatClass;
    FloatValue v;
    v.negative = negative;
    v.magnitude = magnitude;
    v.kind = magnitude == 0                                                   ? FloatClass::Zero
             : magnitude < scaleByPowerOfTwo(Rational(1), f.minExponent()) ? FloatClass::Subnormal
                                                                            : FloatClass::Normal;
    return v;
}

}  // namespace impl

// The next value of f above v (IEEE 754 §5.3.1); v is Zero, Subnormal, Normal or Infinite.
inline FloatValue nextUp(const BinaryFormat& f, bool subnormals, const FloatValue& v) {
    using calculate_core::FloatClass;
    if (v.kind == FloatClass::Infinite) return v.negative ? impl::finiteValue(f, true, impl::largest(f)) : v;
    if (v.kind == FloatClass::Zero) return impl::finiteValue(f, false, impl::smallest(f, subnormals));
    const Rational ulp = scaleByPowerOfTwo(Rational(1), ulpExponent(f, subnormals, v));
    if (!v.negative) {
        if (v.magnitude == impl::largest(f)) {
            FloatValue infinity;
            infinity.kind = FloatClass::Infinite;
            return infinity;
        }
        return impl::finiteValue(f, false, v.magnitude + ulp);
    }
    // Towards zero: below an exact power of two above 2^emin, the binade is twice as dense.
    const long long e = impl::floorLog2(v.magnitude);
    const bool power = v.magnitude == scaleByPowerOfTwo(Rational(1), e);
    const Rational step = power && e > f.minExponent() ? Rational(ulp / 2) : ulp;
    const Rational m = v.magnitude - step;
    if (m == 0 || (!subnormals && m < scaleByPowerOfTwo(Rational(1), f.minExponent()))) return impl::finiteValue(f, true, Rational(0));
    return impl::finiteValue(f, true, m);
}

// The next value of f below v: the negation of nextUp of the negation.
inline FloatValue nextDown(const BinaryFormat& f, bool subnormals, const FloatValue& v) {
    FloatValue flipped = v;
    flipped.negative = !flipped.negative;
    FloatValue r = nextUp(f, subnormals, flipped);
    r.negative = !r.negative;
    return r;
}

}  // namespace calculate_core::detail
