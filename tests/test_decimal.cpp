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

TEST(ExactDigits, DoubleOneTenth) {
    const DecimalDigits d = exactDigits(0.1);
    EXPECT_FALSE(d.negative);
    EXPECT_EQ(d.digits, "1000000000000000055511151231257827021181583404541015625");
    EXPECT_EQ(d.exponent10, -1);
}

TEST(ExactDigits, FloatOneTenth) {
    const DecimalDigits d = exactDigits(0.1f);
    EXPECT_EQ(d.digits, "100000001490116119384765625");
    EXPECT_EQ(d.exponent10, -1);
}

TEST(ExactDigits, LargeAndIntegralValues) {
    EXPECT_EQ(exactDigits(1e23).digits, "99999999999999991611392");
    EXPECT_EQ(exactDigits(1e23).exponent10, 22);
    EXPECT_EQ(exactDigits(1024.0).digits, "1024");
    EXPECT_EQ(exactDigits(1024.0).exponent10, 3);
    const DecimalDigits minusTwoAndAHalf = exactDigits(-2.5);
    EXPECT_TRUE(minusTwoAndAHalf.negative);
    EXPECT_EQ(minusTwoAndAHalf.digits, "25");
    EXPECT_EQ(minusTwoAndAHalf.exponent10, 0);
}

TEST(ExactDigits, Zero) {
    EXPECT_EQ(exactDigits(0.0).digits, "0");
    EXPECT_EQ(exactDigits(0.0).exponent10, 0);
}

TEST(ExactDigits, SmallestSubnormalDoubleHas751Digits) {
    const DecimalDigits d = exactDigits(std::numeric_limits<double>::denorm_min());
    EXPECT_EQ(d.exponent10, -324);
    EXPECT_EQ(d.digits.size(), 751u);
    EXPECT_EQ(d.digits.substr(0, 64),
              "4940656458412465441765687928682213723650598026143247644255856825");
}

TEST(ExactDigits, Binary128OneThird) {
    const DecimalDigits d = exactDigits(Binary128(1) / 3);
    EXPECT_EQ(d.exponent10, -1);
    EXPECT_EQ(d.digits,
              "333333333333333333333333333333333317283917130106367891200183811792272345515819598205098373000510036945343017578125");
}

template <class T>
class ExactDigitsTest : public ::testing::Test {};
TYPED_TEST_SUITE(ExactDigitsTest, test::FloatingTypes, test::TypeNames);

TYPED_TEST(ExactDigitsTest, DigitsReproduceTheValueExactly) {
    using T = TypeParam;
    std::mt19937_64 rng(4);
    for (int i = 0; i < 100; ++i) {
        const T x = test::randomFinite<T>(rng, 100);
        const DecimalDigits d = exactDigits(x);
        ASSERT_EQ(d.digits.find_first_not_of("0123456789"), std::string::npos);
        EXPECT_NE(d.digits.front(), '0');
        EXPECT_NE(d.digits.back(), '0');
        Rational value = Rational(Integer(d.digits));
        const long long shift = d.exponent10 - static_cast<long long>(d.digits.size()) + 1;
        const Integer ten = pow(Integer(10), static_cast<unsigned>(shift < 0 ? -shift : shift));
        value = shift < 0 ? value / Rational(ten) : value * Rational(ten);
        EXPECT_EQ(d.negative ? -value : value, toRational(x));
    }
}

static void expectFraction(const Rational& q, bool negative, const char* numerator,
                           const char* denominator, bool hasDecimal, const char* integerPart,
                           const char* fractionDigits, const char* repeatingDigits) {
    const FractionDigits f = exactFraction(q);
    EXPECT_EQ(f.negative, negative);
    EXPECT_EQ(f.numerator, numerator);
    EXPECT_EQ(f.denominator, denominator);
    EXPECT_EQ(f.hasDecimal, hasDecimal);
    if (hasDecimal) {
        EXPECT_EQ(f.integerPart, integerPart);
        EXPECT_EQ(f.fractionDigits, fractionDigits);
        EXPECT_EQ(f.repeatingDigits, repeatingDigits);
    }
}

TEST(ExactFraction, TerminatingAndRepeating) {
    expectFraction(Rational(1, 3), false, "1", "3", true, "0", "", "3");
    expectFraction(Rational(1, 6), false, "1", "6", true, "0", "1", "6");
    expectFraction(Rational(22, 7), false, "22", "7", true, "3", "", "142857");
    expectFraction(Rational(3, 8), false, "3", "8", true, "0", "375", "");
    expectFraction(Rational(-5, 2), true, "5", "2", true, "2", "5", "");
    expectFraction(Rational(10), false, "10", "1", true, "10", "", "");
    expectFraction(Rational(0), false, "0", "1", true, "0", "", "");
    expectFraction(Rational(-54767, 66192), true, "54767", "66192", false, "", "", "");  // period > 60
}

TEST(ExactFraction, LongPeriodFallsBackToTheFraction) {
    EXPECT_FALSE(exactFraction(Rational(1, 97)).hasDecimal);  // period 96
    EXPECT_TRUE(exactFraction(Rational(1, 97), 96).hasDecimal);
    EXPECT_EQ(exactFraction(Rational(1, 97), 96).repeatingDigits.size(), 96u);
}
