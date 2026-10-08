#include "float_format.hpp"
#include "test_support.hpp"

#include <cfloat>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace calculate_core::detail;

namespace {

void expectSame(const BinaryFormat& a, const BinaryFormat& b) {
    EXPECT_EQ(a.exponentBits, b.exponentBits);
    EXPECT_EQ(a.fractionBits, b.fractionBits);
    EXPECT_EQ(a.explicitLeadingBit, b.explicitLeadingBit);
}

}  // namespace

TEST(BinaryFormat, TheLayoutsOfTheIeeeFormatsAndTheX87) {
    EXPECT_EQ(binary16.storageBits(), 16);
    EXPECT_EQ(binary16.precision(), 11);
    EXPECT_EQ(binary16.bias(), 15);
    EXPECT_EQ(binary16.minExponent(), -14);
    EXPECT_EQ(bfloat16.storageBits(), 16);
    EXPECT_EQ(bfloat16.precision(), 8);
    EXPECT_EQ(bfloat16.bias(), 127);
    EXPECT_EQ(binary32.storageBits(), 32);
    EXPECT_EQ(binary32.precision(), 24);
    EXPECT_EQ(binary64.storageBits(), 64);
    EXPECT_EQ(binary64.precision(), 53);
    EXPECT_EQ(binary64.bias(), 1023);
    EXPECT_EQ(x87Extended.storageBits(), 80);
    EXPECT_EQ(x87Extended.precision(), 64);  // the leading bit is one of the 64 stored ones
    EXPECT_EQ(x87Extended.bias(), 16383);
    EXPECT_EQ(binary128.storageBits(), 128);
    EXPECT_EQ(binary128.precision(), 113);
    EXPECT_EQ(binary256.storageBits(), 256);
    EXPECT_EQ(binary256.exponentBits, 19);
    EXPECT_EQ(binary256.bias(), 262143);
    EXPECT_EQ(binary512.storageBits(), 512);
    EXPECT_EQ(binary512.exponentBits, 23);
    EXPECT_EQ(binary512.precision(), 489);
    EXPECT_EQ(binary512.maxExponent(), 4194303);
}

TEST(BinaryFormat, EachTypeKnowsItsFormat) {
    expectSame(formatOf<float>(), binary32);
    expectSame(formatOf<double>(), binary64);
    expectSame(formatOf<Binary128>(), binary128);
    expectSame(formatOf<Binary256>(), binary256);
    expectSame(formatOf<Binary512>(), binary512);
    if (LDBL_MANT_DIG == 64) expectSame(formatOf<long double>(), x87Extended);
    if (LDBL_MANT_DIG == 113) expectSame(formatOf<long double>(), binary128);
}

template <class T>
class FormatOfTest : public ::testing::Test {};
TYPED_TEST_SUITE(FormatOfTest, test::FloatingTypes, test::TypeNames);

TYPED_TEST(FormatOfTest, MatchesTheTypesTraits) {
    using T = TypeParam;
    const BinaryFormat f = formatOf<T>();
    EXPECT_EQ(f.precision(), precisionBits<T>());
    EXPECT_EQ(f.minExponent(), minExponent<T>());
    EXPECT_EQ(f.maxExponent(), maxExponent<T>());
}

namespace {

Rational pow2(long long e) { return scaleByPowerOfTwo(Rational(1), e); }

}  // namespace

TEST(RoundToFormat, Binary16) {
    const FloatValue tenth = roundToFormat(false, Rational(1, 10), binary16, true);
    EXPECT_EQ(tenth.kind, calculate_core::FloatClass::Normal);
    EXPECT_EQ(tenth.magnitude, Rational(819, 8192));
    EXPECT_EQ(roundToFormat(false, Rational(65519), binary16, true).magnitude, Rational(65504));
    EXPECT_EQ(roundToFormat(false, Rational(65520), binary16, true).kind, calculate_core::FloatClass::Infinite);  // the tie rounds to 2^16
    EXPECT_EQ(roundToFormat(false, pow2(-24), binary16, true).kind, calculate_core::FloatClass::Subnormal);
    EXPECT_EQ(roundToFormat(false, pow2(-25), binary16, true).kind, calculate_core::FloatClass::Zero);  // tie: even is 0
    EXPECT_EQ(roundToFormat(false, 3 * pow2(-26), binary16, true).magnitude, pow2(-24));
    EXPECT_EQ(roundToFormat(false, Rational(1, 3), binary16, true).magnitude, Rational(1365, 4096));
}

TEST(RoundToFormat, WithoutSubnormalsSmallValuesFlushToZero) {
    EXPECT_EQ(roundToFormat(false, pow2(-15), binary16, true).kind, calculate_core::FloatClass::Subnormal);
    const FloatValue flushed = roundToFormat(true, pow2(-15), binary16, false);
    EXPECT_EQ(flushed.kind, calculate_core::FloatClass::Zero);
    EXPECT_TRUE(flushed.negative);  // the sign survives, as in the software types
    EXPECT_EQ(roundToFormat(false, pow2(-14) - pow2(-26), binary16, false).magnitude, pow2(-14));  // rounds up into range
}

TYPED_TEST(FormatOfTest, AgreesWithTheTypesOwnRounding) {
    using T = TypeParam;
    std::mt19937_64 rng(20261003);
    for (int i = 0; i < 200; ++i) {
        const Rational q = toRational(test::randomFinite<T>(rng, 200)) * Rational(7, 5);
        const FloatValue v = roundToFormat(q < 0, abs(q), formatOf<T>(), hasSubnormals<T>());
        EXPECT_EQ(v.negative ? Rational(-v.magnitude) : v.magnitude, toRational(fromRational<T>(q)));
    }
}

namespace {

using calculate_core::FloatClass;

Integer bits(const std::string& hex) { return Integer("0x" + hex); }  // Boost's string constructor: tests only

FloatValue finite(const Rational& q, const BinaryFormat& f) { return roundToFormat(q < 0, abs(q), f, true); }

FloatValue special(FloatClass kind, bool negative = false) {
    FloatValue v;
    v.kind = kind;
    v.negative = negative;
    return v;
}

}  // namespace

TEST(Encode, TheBitsOfATenth) {
    EXPECT_EQ(encode(binary16, finite(Rational(1, 10), binary16)), bits("2E66"));
    EXPECT_EQ(encode(bfloat16, finite(Rational(1, 10), bfloat16)), bits("3DCD"));
    EXPECT_EQ(encode(binary32, finite(Rational(1, 10), binary32)), bits("3DCCCCCD"));
    EXPECT_EQ(encode(binary64, finite(Rational(1, 10), binary64)), bits("3FB999999999999A"));
    EXPECT_EQ(encode(x87Extended, finite(Rational(1, 10), x87Extended)), bits("3FFBCCCCCCCCCCCCCCCD"));
    EXPECT_EQ(encode(binary128, finite(Rational(1, 10), binary128)), bits("3FFB999999999999999999999999999A"));
}

TEST(Encode, OneInEveryFormat) {
    EXPECT_EQ(encode(binary16, finite(Rational(1), binary16)), bits("3C00"));
    EXPECT_EQ(encode(bfloat16, finite(Rational(1), bfloat16)), bits("3F80"));
    EXPECT_EQ(encode(binary32, finite(Rational(1), binary32)), bits("3F800000"));
    EXPECT_EQ(encode(x87Extended, finite(Rational(1), x87Extended)), bits("3FFF8000000000000000"));  // the stored leading 1
    EXPECT_EQ(encode(binary128, finite(Rational(1), binary128)), bits("3FFF0000000000000000000000000000"));
    EXPECT_EQ(encode(binary256, finite(Rational(1), binary256)), bits("3FFFF" + std::string(59, '0')));
    EXPECT_EQ(encode(binary512, finite(Rational(1), binary512)), bits("3FFFFF" + std::string(122, '0')));
}

TEST(Encode, ZerosSubnormalsAndExtremes) {
    EXPECT_EQ(encode(binary64, special(FloatClass::Zero, true)), bits("8000000000000000"));
    EXPECT_EQ(encode(binary16, finite(pow2(-24), binary16)), bits("0001"));
    EXPECT_EQ(encode(binary16, finite(Rational(65504), binary16)), bits("7BFF"));
    EXPECT_EQ(encode(x87Extended, finite(pow2(-16445), x87Extended)), bits("00000000000000000001"));  // no leading bit below the normal range
    EXPECT_EQ(encode(x87Extended, finite(pow2(-16382), x87Extended)), bits("00018000000000000000"));
    EXPECT_EQ(encode(binary128, finite(pow2(-16382), binary128)), bits("00010000000000000000000000000000"));
}

TEST(Encode, InfinitiesAndNaNs) {
    EXPECT_EQ(encode(binary32, special(FloatClass::Infinite)), bits("7F800000"));
    EXPECT_EQ(encode(binary32, special(FloatClass::Infinite, true)), bits("FF800000"));
    EXPECT_EQ(encode(binary32, special(FloatClass::QuietNaN)), bits("7FC00000"));
    FloatValue signaling = special(FloatClass::SignalingNaN);
    signaling.payload = 1;
    EXPECT_EQ(encode(binary32, signaling), bits("7F800001"));
    EXPECT_EQ(encode(binary16, special(FloatClass::QuietNaN)), bits("7E00"));
    EXPECT_EQ(encode(x87Extended, special(FloatClass::Infinite)), bits("7FFF8000000000000000"));
    EXPECT_EQ(encode(x87Extended, special(FloatClass::QuietNaN)), bits("7FFFC000000000000000"));
    EXPECT_EQ(encode(binary128, special(FloatClass::QuietNaN)), bits("7FFF8000000000000000000000000000"));
}
