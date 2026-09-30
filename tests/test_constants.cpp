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
