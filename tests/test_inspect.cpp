// Black-box tests: only the public header.
#include <calculate-core/calculate-core.hpp>

#include <gtest/gtest.h>

#include <cfloat>
#include <chrono>
#include <string>
#include <vector>

using namespace calculate_core;

namespace {

const std::vector<FloatFormatInfo>& formats() {
    static const std::vector<FloatFormatInfo> list = floatFormats();
    return list;
}

// The first entry with this encoding.
[[maybe_unused]] const FloatFormatInfo& entry(FloatFormat format) {  // used from the next tests on
    for (const FloatFormatInfo& f : formats())
        if (f.format == format) return f;
    ADD_FAILURE() << "no such format";
    return formats().front();
}

// The entry of a number type's own storage.
const FloatFormatInfo& entryOf(NumberType type) {
    for (const FloatFormatInfo& f : formats())
        if (f.type == type) return f;
    ADD_FAILURE() << "no such type";
    return formats().front();
}

}  // namespace

TEST(FloatFormats, TheInspectorListsTheTypesAndTheDisplayFormats) {
    const std::vector<FloatFormatInfo>& list = formats();
    ASSERT_EQ(list.size(), LDBL_MANT_DIG == 64 ? 8u : 9u);
    EXPECT_EQ(list[0].name, "binary16");
    EXPECT_FALSE(list[0].type);
    EXPECT_EQ(list[1].name, "bfloat16");
    EXPECT_EQ(list[2].name, "binary32");
    EXPECT_EQ(list[2].type, NumberType::Float);
    EXPECT_EQ(list[3].type, NumberType::Double);
    EXPECT_EQ(list[3].bias, 1023);
    EXPECT_EQ(list[4].type, NumberType::LongDouble);
    for (const FloatFormatInfo& f : list) EXPECT_EQ(f.storageBits, 1 + f.exponentBits + f.fractionBits);
    const FloatFormatInfo& quad = entryOf(NumberType::Binary128);
    EXPECT_EQ(quad.name, "binary128");
    EXPECT_FALSE(quad.subnormals);
    EXPECT_EQ(quad.precisionBits, 113);
    EXPECT_TRUE(entryOf(NumberType::Double).subnormals);
    EXPECT_EQ(entryOf(NumberType::Binary512).storageBits, 512);
    if (LDBL_MANT_DIG == 64) {
        EXPECT_EQ(list[4].name, "x87 extended");
        EXPECT_TRUE(list[4].explicitLeadingBit);
        EXPECT_EQ(list[4].fractionBits, 64);
    } else {
        EXPECT_EQ(list[4].name, "binary128");
        EXPECT_TRUE(list[4].subnormals);
        EXPECT_EQ(list[5].name, "x87 extended");
        EXPECT_FALSE(list[5].type);  // display only here
    }
}
