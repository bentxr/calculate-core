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

TYPED_TEST(ConversionTest, MatchesCorrectlyRoundedDivision) {
    using T = TypeParam;  // hardware and Boost division are correctly rounded
    for (int a = -40; a <= 40; ++a)
        for (int b = 1; b <= 40; ++b)
            EXPECT_EQ(fromRational<T>(Rational(a, b)), T(a) / T(b)) << a << "/" << b;
}

TYPED_TEST(ConversionTest, RoundTripIsExact) {
    using T = TypeParam;
    std::mt19937_64 rng(20260930);
    for (int i = 0; i < 300; ++i) {
        const T x = test::randomFinite<T>(rng, 200);
        EXPECT_EQ(fromRational<T>(toRational(x)), x);
    }
}

TYPED_TEST(ConversionTest, TiesGoToEven) {
    using T = TypeParam;
    using std::ldexp;
    const int p = precisionBits<T>();
    const Rational one(1);
    const Rational halfUlp = scaleByPowerOfTwo(one, -p);  // ulp(1) = 2^(1-p)
    EXPECT_EQ(fromRational<T>(one + halfUlp), T(1));
    EXPECT_EQ(fromRational<T>(one + 3 * halfUlp), T(1) + ldexp(T(1), 2 - p));
    EXPECT_EQ(fromRational<T>(one + halfUlp + scaleByPowerOfTwo(one, -3 * p)),
              T(1) + ldexp(T(1), 1 - p));
}

TYPED_TEST(ConversionTest, OverflowGivesInfinity) {
    using T = TypeParam;
    const Rational tooBig = scaleByPowerOfTwo(Rational(1), maxExponent<T>() + 1);
    EXPECT_FALSE(isFinite(fromRational<T>(tooBig)));
    EXPECT_FALSE(isFinite(fromRational<T>(-tooBig)));
    EXPECT_LT(fromRational<T>(-tooBig), T(0));
    const T largest = (std::numeric_limits<T>::max)();
    const Rational aQuarterUlpAbove = toRational(largest)
        + scaleByPowerOfTwo(Rational(1), maxExponent<T>() - precisionBits<T>() - 1);
    EXPECT_EQ(fromRational<T>(aQuarterUlpAbove), largest);
}

TYPED_TEST(ConversionTest, UnderflowFollowsTheType) {
    using T = TypeParam;
    const Rational smallestNormal = scaleByPowerOfTwo(Rational(1), minExponent<T>());
    EXPECT_EQ(fromRational<T>(smallestNormal), (std::numeric_limits<T>::min)());
    if (hasSubnormals<T>()) {
        const Rational tiniest =
            scaleByPowerOfTwo(Rational(1), minExponent<T>() - precisionBits<T>() + 1);
        EXPECT_EQ(fromRational<T>(tiniest), std::numeric_limits<T>::denorm_min());
        EXPECT_EQ(fromRational<T>(tiniest / 2), T(0));  // tie between 0 and denorm_min: even
        EXPECT_EQ(fromRational<T>(tiniest * 3 / 4), std::numeric_limits<T>::denorm_min());
    } else {
        EXPECT_EQ(fromRational<T>(smallestNormal / 2), T(0));  // flushed, like the type itself
    }
}

TEST(Conversion, ExactCastBetweenTypes) {
    EXPECT_EQ(exactCast<Ruler>(0.1), Ruler(0.1));  // Boost's constructor from double is exact
    EXPECT_EQ(exactCast<double>(Binary128(1) / 3), 1.0 / 3.0);
    EXPECT_EQ(exactCast<Rational>(0.5f), Rational(1, 2));
}

TEST(Numbers, FloorOfARational) {
    EXPECT_EQ(floorOf(Rational(7, 3)), Integer(2));
    EXPECT_EQ(floorOf(Rational(-7, 3)), Integer(-3));
    EXPECT_EQ(floorOf(Rational(-6, 3)), Integer(-2));
    EXPECT_EQ(floorOf(Rational(0)), Integer(0));
    EXPECT_EQ(floorOf(Rational(-1, 1000)), Integer(-1));
    EXPECT_EQ(floorOf(Rational(999, 1000)), Integer(0));
}
