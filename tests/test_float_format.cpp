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
