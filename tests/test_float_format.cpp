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

TEST(Decode, InvertsEncodeInEveryFormat) {
    std::mt19937_64 rng(4);
    for (const BinaryFormat& f : {binary16, bfloat16, binary32, binary64, x87Extended, binary128, binary256, binary512})
        for (int i = 0; i < 100; ++i) {
            const Rational ratio(Integer(rng() >> 1), Integer(rng() | 1));
            const Rational q = scaleByPowerOfTwo(ratio, static_cast<long long>(rng() % 64) - 32);
            const FloatValue v = finite(i % 2 ? Rational(-q) : q, f);
            const FloatValue back = decode(f, encode(f, v));
            EXPECT_EQ(back.kind, v.kind);
            EXPECT_EQ(back.negative, v.negative);
            EXPECT_EQ(back.magnitude, v.magnitude);
        }
}

TEST(Decode, Classes) {
    const FloatValue tiny = decode(binary32, bits("00000001"));
    EXPECT_EQ(tiny.kind, FloatClass::Subnormal);
    EXPECT_EQ(tiny.magnitude, pow2(-149));
    EXPECT_EQ(decode(binary32, bits("80000000")).kind, FloatClass::Zero);
    EXPECT_TRUE(decode(binary32, bits("80000000")).negative);
    EXPECT_EQ(decode(binary32, bits("7F800000")).kind, FloatClass::Infinite);
    const FloatValue quiet = decode(binary32, bits("FFC00001"));
    EXPECT_EQ(quiet.kind, FloatClass::QuietNaN);
    EXPECT_TRUE(quiet.negative);
    EXPECT_EQ(quiet.payload, 1);
    const FloatValue signaling = decode(binary32, bits("7F800001"));
    EXPECT_EQ(signaling.kind, FloatClass::SignalingNaN);
    EXPECT_EQ(signaling.payload, 1);
}

TEST(Decode, NoncanonicalX87Encodings) {
    const FloatValue pseudoDenormal = decode(x87Extended, bits("00008000000000000000"));
    EXPECT_EQ(pseudoDenormal.kind, FloatClass::Noncanonical);
    EXPECT_EQ(pseudoDenormal.note, "pseudo-denormal");
    EXPECT_EQ(pseudoDenormal.magnitude, pow2(-16382));  // what the 387 reads
    EXPECT_EQ(decode(x87Extended, bits("3FFF4000000000000000")).note, "unnormal");
    EXPECT_EQ(decode(x87Extended, bits("7FFF0000000000000000")).note, "pseudo-infinity");
    EXPECT_EQ(decode(x87Extended, bits("7FFF0000000000000001")).note, "pseudo-NaN");
    EXPECT_EQ(decode(x87Extended, bits("7FFFC000000000000000")).kind, FloatClass::QuietNaN);
    EXPECT_EQ(decode(x87Extended, bits("7FFF8000000000000001")).kind, FloatClass::SignalingNaN);
}

namespace {

// The bytes of a hardware value as an integer (an oracle for tests only; both targets are
// little-endian; the x87 format uses the first 10 bytes of its 16).
template <class T>
Integer memoryBits(const T& x) {
    unsigned char bytes[sizeof(T)];
    std::memcpy(bytes, &x, sizeof(T));
    Integer n = 0;
    for (int i = formatOf<T>().storageBits() / 8 - 1; i >= 0; --i) {  // in place: GCC misreads n * 256 + b (array-bounds)
        n <<= 8;
        n += bytes[i];
    }
    return n;
}

}  // namespace

using HardwareTypes = ::testing::Types<float, double, long double>;
template <class T>
class HardwareBitsTest : public ::testing::Test {};
TYPED_TEST_SUITE(HardwareBitsTest, HardwareTypes, test::TypeNames);

TYPED_TEST(HardwareBitsTest, EncodeMatchesTheBytesInMemory) {
    using T = TypeParam;
    using L = std::numeric_limits<T>;
    std::mt19937_64 rng(5);
    std::vector<T> values{T(0), -T(0), T(1), T(0.1), L::denorm_min(), (L::min)(), -(L::max)(),
                          L::infinity(), -L::infinity(), L::quiet_NaN()};
    for (int i = 0; i < 300; ++i) values.push_back(test::randomFinite<T>(rng, maxExponent<T>() - 1));
    for (int i = 0; i < 50; ++i) values.push_back(std::ldexp(test::randomFinite<T>(rng, 0), minExponent<T>() - 5));  // subnormals
    for (const T& x : values) EXPECT_EQ(encode(formatOf<T>(), valueOf(x)), memoryBits(x));
}

TEST(ValueOf, SoftwareTypesKeepTheSignOfZero) {
    EXPECT_TRUE(valueOf(-Binary128(0)).negative);
    EXPECT_EQ(valueOf(-Binary128(0)).kind, FloatClass::Zero);
    EXPECT_EQ(valueOf(Binary128(1) / 3).magnitude, toRational(Binary128(1) / 3));
    EXPECT_EQ(valueOf((std::numeric_limits<Binary256>::min)()).kind, FloatClass::Normal);
    EXPECT_EQ(valueOf(std::numeric_limits<Binary512>::infinity()).kind, FloatClass::Infinite);
}

TYPED_TEST(HardwareBitsTest, NeighboursAgreeWithNextafter) {
    using T = TypeParam;
    using L = std::numeric_limits<T>;
    const BinaryFormat f = formatOf<T>();
    std::mt19937_64 rng(6);
    std::vector<T> values{T(0), -T(0), T(1), -T(1), T(0.1), L::denorm_min(), (L::min)(), -(L::min)(),
                          (L::max)(), -(L::max)(), L::infinity(), -L::infinity()};
    for (int i = 0; i < 200; ++i) values.push_back(test::randomFinite<T>(rng, 100));
    for (const T& x : values) {
        EXPECT_EQ(encode(f, nextUp(f, true, valueOf(x))), memoryBits(std::nextafter(x, L::infinity())));
        EXPECT_EQ(encode(f, nextDown(f, true, valueOf(x))), memoryBits(std::nextafter(x, -L::infinity())));
    }
}

TEST(Neighbours, SoftwareTypesSkipTheSubnormalRange) {
    const FloatValue minNormal = valueOf((std::numeric_limits<Binary128>::min)());
    EXPECT_EQ(nextDown(binary128, false, minNormal).kind, FloatClass::Zero);
    EXPECT_EQ(encode(binary128, nextDown(binary128, true, minNormal)), bits("0000FFFFFFFFFFFFFFFFFFFFFFFFFFFF"));  // IEEE binary128 has subnormals
    EXPECT_EQ(encode(binary128, nextUp(binary128, false, valueOf(Binary128(0)))), bits("00010000000000000000000000000000"));
    EXPECT_EQ(encode(binary128, nextDown(binary128, false, valueOf(Binary128(1)))), bits("3FFEFFFFFFFFFFFFFFFFFFFFFFFFFFFF"));
    EXPECT_EQ(nextUp(binary128, false, valueOf((std::numeric_limits<Binary128>::max)())).kind, FloatClass::Infinite);
}

TEST(Neighbours, UlpIsTheSpacingAtTheValue) {
    EXPECT_EQ(ulpExponent(binary64, true, finite(Rational(1, 10), binary64)), -56);
    EXPECT_EQ(ulpExponent(binary64, true, finite(Rational(1), binary64)), -52);
    EXPECT_EQ(ulpExponent(binary64, true, special(FloatClass::Zero)), -1074);
    EXPECT_EQ(ulpExponent(binary128, false, special(FloatClass::Zero)), -16382);  // no subnormals: the gap above 0
    EXPECT_EQ(ulpExponent(binary32, true, finite(pow2(-149), binary32)), -149);
}

TEST(TerminatingDigits, ExactDecimals) {
    const DecimalDigits tenth = terminatingDigits(Rational(1, 10));
    EXPECT_EQ(tenth.digits, "1");
    EXPECT_EQ(tenth.exponent10, -1);
    EXPECT_FALSE(tenth.negative);
    const DecimalDigits error = terminatingDigits(toRational(0.1) - Rational(1, 10));
    EXPECT_EQ(error.digits, "55511151231257827021181583404541015625");
    EXPECT_EQ(error.exponent10, -18);
    const DecimalDigits eighth = terminatingDigits(Rational(-3, 8));
    EXPECT_TRUE(eighth.negative);
    EXPECT_EQ(eighth.digits, "375");
    EXPECT_EQ(eighth.exponent10, -1);
    EXPECT_EQ(terminatingDigits(Rational(0)).digits, "0");
    EXPECT_EQ(terminatingDigits(Rational(1200)).digits, "12");
    EXPECT_EQ(terminatingDigits(Rational(1200)).exponent10, 3);
}

TEST(TerminatingDigits, AgreeWithExactDigitsForBinaryValues) {
    for (const double x : {0.1, 1e300, 5e-324, -2.5}) {
        const DecimalDigits a = terminatingDigits(toRational(x));
        const DecimalDigits b = exactDigits(x);
        EXPECT_EQ(a.digits, b.digits);
        EXPECT_EQ(a.exponent10, b.exponent10);
        EXPECT_EQ(a.negative, b.negative);
    }
}

TEST(TerminatingDigits, LeaveOutValuesBeyondTheLimit) {
    EXPECT_EQ(terminatingDigits(pow2(-1074), 1000).digits.size(), 751u);
    EXPECT_EQ(terminatingDigits(pow2(-1074), 500).digits, "");
    EXPECT_EQ(terminatingDigits(pow2(-4194302)).digits, "");  // binary512's smallest normal: millions of digits
}

TEST(BinaryForm, ExactlyAsSignificandTimesAPowerOfTwo) {
    const BinaryForm tiny = binaryForm(pow2(-4194302));
    EXPECT_EQ(tiny.significand, "1");
    EXPECT_EQ(tiny.exponent2, -4194302);
    const BinaryForm three = binaryForm(Rational(-3, 8));
    EXPECT_TRUE(three.negative);
    EXPECT_EQ(three.significand, "3");  // odd: the power of two holds every factor 2
    EXPECT_EQ(three.exponent2, -3);
    EXPECT_EQ(binaryForm(Rational(40)).significand, "5");
    EXPECT_EQ(binaryForm(Rational(40)).exponent2, 3);
    EXPECT_EQ(binaryForm(Rational(0)).significand, "0");
    EXPECT_EQ(binaryForm(Rational(0)).exponent2, 0);  // zero is 0 × 2^0
}

TEST(BaseLiteral, Prefixes) {
    const auto hex = parseBaseLiteral("0xFF");
    ASSERT_TRUE(hex);
    EXPECT_EQ(hex->significand, 255);
    EXPECT_EQ(hex->exponent2, 0);
    EXPECT_EQ(parseBaseLiteral("0b1010")->significand, 10);
    EXPECT_EQ(parseBaseLiteral("0o17")->significand, 15);
    EXPECT_EQ(parseBaseLiteral("0X1f")->significand, 31);
    const auto fractional = parseBaseLiteral("0x1.8p3");  // 1.5 × 2^3 = 12
    EXPECT_EQ(fractional->significand, 24);
    EXPECT_EQ(fractional->exponent2, -1);
    EXPECT_EQ(parseBaseLiteral("0x1p-1074")->exponent2, -1074);
    EXPECT_EQ(parseBaseLiteral("0b0.1")->exponent2, -1);
    EXPECT_EQ(parseBaseLiteral("0o0.4")->exponent2, -3);
    for (const char* bad : {"0x", "0b102", "0xG", "0o8", "0x1.8p", "12"}) EXPECT_FALSE(parseBaseLiteral(bad)) << bad;
}

TEST(BaseExpansion, ExactDigitsInAnyBase) {
    const BaseDigits hex = baseExpansion(toRational(0.1), 16);
    EXPECT_EQ(hex.integerPart, "0");
    EXPECT_EQ(hex.fractionDigits, "1999999999999A");
    EXPECT_EQ(hex.repeatingDigits, "");
    EXPECT_TRUE(hex.complete);
    EXPECT_EQ(baseExpansion(toRational(0.1), 12).fractionDigits, "124972497249724A76232B004276");
    EXPECT_EQ(baseExpansion(toRational(0.1), 8).fractionDigits, "0631463146314631464");
    const BaseDigits half = baseExpansion(Rational(1, 2), 3);
    EXPECT_EQ(half.fractionDigits, "");
    EXPECT_EQ(half.repeatingDigits, "1");
    EXPECT_EQ(baseExpansion(Rational(1, 3), 2).repeatingDigits, "01");
    const BaseDigits minusTen = baseExpansion(Rational(-10), 2);
    EXPECT_TRUE(minusTen.negative);
    EXPECT_EQ(minusTen.integerPart, "1010");
    EXPECT_EQ(baseExpansion(Rational(5, 2), 32).fractionDigits, "G");
    EXPECT_FALSE(baseExpansion(Rational(1, 997), 10).complete);  // its period is 166 digits
}
