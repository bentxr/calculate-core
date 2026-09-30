#include "test_support.hpp"

#include <cfloat>
#include <cmath>

using namespace calculate_core::detail;

TEST(IeeeFormat, PrecisionFollowsTheInterchangeFormula) {
    EXPECT_EQ(ieeeExponentBits(128), 15);
    EXPECT_EQ(ieeePrecision(128), 113);
    EXPECT_EQ(ieeePrecision(256), 237);
    EXPECT_EQ(ieeePrecision(512), 489);
    EXPECT_EQ(ieeePrecision(1024), 997);
    EXPECT_EQ(ieeePrecision(2048), 2017);
    EXPECT_EQ(ieeeMaxExponent(128), 16383);
    EXPECT_EQ(ieeeMaxExponent(256), 262143);
    EXPECT_EQ(ieeeMaxExponent(512), 4194303);
}

TEST(Traits, SoftwareTypesHaveTheirFormatsPrecisionAndRange) {
    EXPECT_EQ(precisionBits<Binary128>(), 113);
    EXPECT_EQ(maxExponent<Binary128>(), 16383);
    EXPECT_EQ(minExponent<Binary128>(), -16382);
    EXPECT_EQ(precisionBits<Binary512>(), 489);
    EXPECT_EQ(maxExponent<Binary512>(), 4194303);
    EXPECT_EQ(precisionBits<Ruler>(), 997);
    EXPECT_EQ(precisionBits<RulerCheck>(), 2017);
}

TEST(Traits, HardwareTypesHaveIeeeExponentRanges) {
    EXPECT_EQ(maxExponent<float>(), 127);
    EXPECT_EQ(minExponent<float>(), -126);
    EXPECT_EQ(maxExponent<double>(), 1023);
    EXPECT_EQ(minExponent<double>(), -1022);
    EXPECT_EQ(precisionBits<long double>(), LDBL_MANT_DIG);
}

TEST(Traits, UnitRoundoffIsHalfTheMachineEpsilon) {
    EXPECT_EQ(unitRoundoff<float>(), std::ldexp(1.0f, -24));
    EXPECT_EQ(unitRoundoff<double>(), std::ldexp(1.0, -53));
    EXPECT_EQ(unitRoundoff<Binary256>(), ldexp(Binary256(1), -237));
    EXPECT_EQ(unitRoundoff<Rational>(), 0);
}

TEST(Traits, OnlyHardwareTypesHaveSubnormals) {
    EXPECT_TRUE(hasSubnormals<float>());
    EXPECT_TRUE(hasSubnormals<double>());
    EXPECT_TRUE(hasSubnormals<long double>());
    EXPECT_FALSE(hasSubnormals<Binary128>());
    EXPECT_FALSE(hasSubnormals<Ruler>());
}

TEST(Traits, ExactnessAndFiniteness) {
    EXPECT_TRUE(isExact<Rational>);
    EXPECT_FALSE(isExact<double>);
    EXPECT_FALSE(isExact<Binary128>);
    EXPECT_TRUE(isFinite(1.0));
    EXPECT_FALSE(isFinite(std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(isFinite(Binary128(1) / Binary128(0)));
    EXPECT_TRUE(isFinite(Rational(1, 3)));
}

template <class T>
class ConversionTest : public ::testing::Test {};
TYPED_TEST_SUITE(ConversionTest, test::FloatingTypes, test::TypeNames);

TYPED_TEST(ConversionTest, SimpleValuesAreExact) {
    using T = TypeParam;
    EXPECT_EQ(toRational(T(0.5)), Rational(1, 2));
    EXPECT_EQ(toRational(T(-3)), Rational(-3));
    EXPECT_EQ(toRational(T(0)), Rational(0));
}

TYPED_TEST(ConversionTest, OneThirdHasTheExpectedPowerOfTwoDenominator) {
    using T = TypeParam;
    const Rational q = toRational(T(1) / T(3));
    const Integer d = denominator(q);
    EXPECT_EQ(d & (d - 1), 0);                              // a power of two
    EXPECT_EQ(msb(d), unsigned(precisionBits<T>() + 1));    // 1/3 = 1.0101..b x 2^-2, last bit set
}

TEST(Conversion, SubnormalDoubleIsExact) {
    EXPECT_EQ(toRational(std::numeric_limits<double>::denorm_min()),
              scaleByPowerOfTwo(Rational(1), -1074));
}

TEST(Conversion, RationalIsItsOwnExactValue) {
    EXPECT_EQ(toRational(Rational(7, 3)), Rational(7, 3));
}

TEST(Conversion, ScaleByPowerOfTwo) {
    EXPECT_EQ(scaleByPowerOfTwo(Rational(3), 4), Rational(48));
    EXPECT_EQ(scaleByPowerOfTwo(Rational(3), -4), Rational(3, 16));
    EXPECT_EQ(scaleByPowerOfTwo(Rational(-5, 7), 0), Rational(-5, 7));
}
