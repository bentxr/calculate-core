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
