#include "test_support.hpp"

using namespace calculate_core::detail;

namespace {

// Integer fixed-point series, independent of the table: result ~ value * 2^bits.
Integer arctanInverse(unsigned x, int bits, bool hyperbolic) {
    Integer power = (Integer(1) << bits) / x;  // 2^bits / x^(2k+1)
    const Integer x2 = Integer(x) * x;
    Integer sum = 0;
    for (unsigned k = 0; power != 0; ++k) {
        const Integer term = power / (2 * k + 1);
        sum += (hyperbolic || k % 2 == 0) ? term : Integer(-term);
        power /= x2;
    }
    return sum;
}

Integer seriesValue(ConstantId id, int bits) {
    const Integer pi = 16 * arctanInverse(5, bits, false) - 4 * arctanInverse(239, bits, false);
    const Integer ln2 = 2 * arctanInverse(3, bits, true);
    switch (id) {
    case ConstantId::Pi: return pi;
    case ConstantId::TwoOverPi: return (Integer(1) << (2 * bits + 1)) / pi;
    case ConstantId::Ln2: return ln2;
    case ConstantId::Ln10: return 3 * ln2 + 2 * arctanInverse(9, bits, true);  // 10 = 8 * 1.25
    case ConstantId::E: {
        Integer term = Integer(1) << bits, sum = 0;
        for (unsigned k = 1; term != 0; ++k) { sum += term; term /= k; }
        return sum;
    }
    default: break;  // the others are checked by their own tests
    }
    return 0;
}

}  // namespace

TEST(Constants, TableMatchesIndependentSeries) {
    const int guard = 64;
    for (ConstantId id : {ConstantId::Pi, ConstantId::TwoOverPi, ConstantId::Ln2,
                          ConstantId::Ln10, ConstantId::E}) {
        const Integer series = seriesValue(id, constantFractionBits + guard) >> guard;
        const Integer table = constantMantissa(id);
        const Integer difference = series > table ? Integer(series - table) : Integer(table - series);
        EXPECT_LE(difference, 2) << static_cast<int>(id);
    }
}

TEST(Constants, DoubleValuesAreCorrectlyRounded) {
    EXPECT_EQ(constantValue<double>(ConstantId::Pi), 0x1.921fb54442d18p+1);
    EXPECT_EQ(constantValue<double>(ConstantId::TwoOverPi), 0x1.45f306dc9c883p-1);
    EXPECT_EQ(constantValue<double>(ConstantId::Ln2), 0x1.62e42fefa39efp-1);
    EXPECT_EQ(constantValue<double>(ConstantId::Ln10), 0x1.26bb1bbb55516p+1);
    EXPECT_EQ(constantValue<double>(ConstantId::E), 0x1.5bf0a8b145769p+1);
}

TEST(Constants, FloatValuesAreCorrectlyRounded) {
    EXPECT_EQ(constantValue<float>(ConstantId::Pi), 0x1.921fb6p+1f);
    EXPECT_EQ(constantValue<float>(ConstantId::TwoOverPi), 0x1.45f306p-1f);
    EXPECT_EQ(constantValue<float>(ConstantId::Ln2), 0x1.62e430p-1f);
    EXPECT_EQ(constantValue<float>(ConstantId::Ln10), 0x1.26bb1cp+1f);
    EXPECT_EQ(constantValue<float>(ConstantId::E), 0x1.5bf0a8p+1f);
}

TEST(Constants, RationalIsTheTableValue) {
    EXPECT_EQ(constantRational(ConstantId::Pi),
              Rational(constantMantissa(ConstantId::Pi)) / Rational(Integer(1) << constantFractionBits));
}

namespace {

// floor(constant * 2^bits) with exact integers only.
Integer algebraicValue(ConstantId id, int bits) {
    switch (id) {
    case ConstantId::Sqrt2: return sqrt(Integer(2) << (2 * bits));
    case ConstantId::Phi: return ((Integer(1) << bits) + sqrt(Integer(5) << (2 * bits))) >> 1;
    case ConstantId::Plastic: {  // the real root of x^3 = x + 1: the largest X with X^3 - X 2^(2b) - 2^(3b) <= 0
        const Integer square = Integer(1) << (2 * bits);
        const Integer cube = Integer(1) << (3 * bits);
        Integer lo = Integer(1) << bits;
        Integer hi = Integer(2) << bits;
        while (hi - lo > 1) {
            const Integer mid = (lo + hi) >> 1;
            if (mid * mid * mid - mid * square - cube <= 0) lo = mid;
            else hi = mid;
        }
        return lo;
    }
    default: return 0;
    }
}

}  // namespace

TEST(Constants, AlgebraicConstantsAreTheirTruncatedValues) {
    for (ConstantId id : {ConstantId::Sqrt2, ConstantId::Phi, ConstantId::Plastic})
        EXPECT_EQ(constantMantissa(id), algebraicValue(id, constantFractionBits)) << static_cast<int>(id);
}

TEST(Constants, AlgebraicConstantsInDoubleAndFloat) {
    EXPECT_EQ(constantValue<double>(ConstantId::Sqrt2), 0x1.6a09e667f3bcdp+0);
    EXPECT_EQ(constantValue<double>(ConstantId::Phi), 0x1.9e3779b97f4a8p+0);
    EXPECT_EQ(constantValue<double>(ConstantId::Plastic), 0x1.5320b74eca44bp+0);
    EXPECT_EQ(constantValue<float>(ConstantId::Sqrt2), 0x1.6a09e6p+0f);
    EXPECT_EQ(constantValue<float>(ConstantId::Phi), 0x1.9e377ap+0f);
    EXPECT_EQ(constantValue<float>(ConstantId::Plastic), 0x1.5320b8p+0f);
}
