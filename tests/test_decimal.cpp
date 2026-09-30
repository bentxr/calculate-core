#include "test_support.hpp"

#include <cmath>

using namespace calculate_core::detail;

static DecimalLiteral literal(const char* text) {
    const auto d = parseDecimal(text);
    EXPECT_TRUE(d.has_value()) << text;
    return d.value_or(DecimalLiteral{});
}

TEST(Decimal, ParsesTheLiteralGrammar) {
    const DecimalLiteral d = literal("12.50e-3");
    EXPECT_EQ(d.significand, 1250);
    EXPECT_EQ(d.exponent10, -5);
    EXPECT_TRUE(parseDecimal(".5"));
    EXPECT_TRUE(parseDecimal("5."));
    EXPECT_TRUE(parseDecimal("1E+3"));
    EXPECT_TRUE(parseDecimal("007"));
    for (const char* bad : {"", ".", "e3", "1e", "1e+", "1.2.3", "1a", "-1", " 1", "1 ", "1e3.5"})
        EXPECT_FALSE(parseDecimal(bad)) << bad;
}

TEST(Decimal, HugeExponentsSaturateInsteadOfOverflowing) {
    EXPECT_EQ(literal("1e99999999999999999999").exponent10, 1000000000000000LL);
    EXPECT_EQ(literal("1e-99999999999999999999").exponent10, -1000000000000000LL);
}

TEST(Decimal, ToRationalIsExact) {
    EXPECT_EQ(toRational(literal("0.1")), Rational(1, 10));
    EXPECT_EQ(toRational(literal("2.5e3")), Rational(2500));
    EXPECT_EQ(toRational(literal("1e-3")), Rational(1, 1000));
    EXPECT_EQ(toRational(literal("0")), Rational(0));
}

TEST(Decimal, HardwareConversionMatchesCompilerLiterals) {
    EXPECT_EQ(decimalTo<double>(literal("0.1")), 0.1);
    EXPECT_EQ(decimalTo<double>(literal("1e23")), 1e23);
    EXPECT_EQ(decimalTo<double>(literal("2.2250738585072011e-308")), 2.2250738585072011e-308);
    EXPECT_EQ(decimalTo<double>(literal("4.9e-324")), 4.9e-324);
    EXPECT_EQ(decimalTo<double>(literal("1.7976931348623157e308")), 1.7976931348623157e308);
    EXPECT_EQ(decimalTo<double>(literal("123456789012345678901234567890")),
              123456789012345678901234567890.0);
    EXPECT_EQ(decimalTo<float>(literal("0.1")), 0.1f);
    EXPECT_EQ(decimalTo<float>(literal("3.4028235e38")), 3.4028235e38f);
    EXPECT_EQ(decimalTo<float>(literal("1e-45")), 1e-45f);
    EXPECT_EQ(decimalTo<long double>(literal("0.1")), 0.1L);
    EXPECT_EQ(decimalTo<long double>(literal("1e4000")), 1e4000L);
}

TEST(Decimal, OutOfRangeLiterals) {
    EXPECT_TRUE(std::isinf(decimalTo<double>(literal("1e309"))));
    EXPECT_EQ(decimalTo<double>(literal("1e-400")), 0.0);
    EXPECT_TRUE(std::isinf(decimalTo<double>(literal("1e99999999999999999999"))));  // fast path
    EXPECT_EQ(decimalTo<double>(literal("1e-99999999999999999999")), 0.0);
    EXPECT_FALSE(isFinite(decimalTo<Binary128>(literal("1e5000"))));
}

template <class T>
class DecimalBoostTest : public ::testing::Test {};
using BoostTypes = ::testing::Types<Binary128, Binary256, Binary512>;
TYPED_TEST_SUITE(DecimalBoostTest, BoostTypes, test::TypeNames);

TYPED_TEST(DecimalBoostTest, MatchesBoostsCorrectlyRoundedStringConversion) {
    using T = TypeParam;  // Boost's string constructor is correctly rounded (tests only)
    for (const char* text : {"0.1", "3.14159265358979323846264338327950288419716939937510",
                             "1e-300", "6.02214076e23", "2.5", "0.3333333333333333333333333333333333333"})
        EXPECT_EQ(decimalTo<T>(literal(text)), T(text)) << text;
}

TEST(Decimal, RationalTargetIsExact) {
    EXPECT_EQ(decimalTo<Rational>(literal("0.125")), Rational(1, 8));
}
