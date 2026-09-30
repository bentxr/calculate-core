// Black-box tests: only the public header.
#include <calculate-core/calculate-core.hpp>

#include <gtest/gtest.h>

#include <cfloat>
#include <chrono>

using namespace calculate_core;

TEST(Api, TheTypeMenuDescribesThisBuild) {
    const std::vector<TypeInfo> types = numberTypes();
    ASSERT_EQ(types.size(), 7u);
    for (std::size_t i = 0; i < types.size(); ++i) EXPECT_EQ(static_cast<std::size_t>(types[i].type), i);
    const TypeInfo& d = types[1];
    EXPECT_EQ(d.label, "Double");
    EXPECT_EQ(d.cppName, "double");
    EXPECT_EQ(d.storageBits, 64);
    EXPECT_EQ(d.precisionBits, 53);
    EXPECT_EQ(d.decimalDigits, 16);
    EXPECT_EQ(d.note, "");
    const TypeInfo& ld = types[2];
    EXPECT_EQ(ld.cppName, "long double");
    EXPECT_EQ(ld.precisionBits, LDBL_MANT_DIG);
    if (LDBL_MANT_DIG == 64) {
        EXPECT_EQ(ld.storageBits, 80);
        EXPECT_EQ(ld.decimalDigits, 19);
        EXPECT_EQ(ld.note, "");
    }
    if (LDBL_MANT_DIG == 113) {
        EXPECT_EQ(ld.storageBits, 128);
        EXPECT_EQ(ld.note, "same format as binary128 here");
    }
    EXPECT_EQ(types[3].label, "Exact");
    EXPECT_EQ(types[3].note, "no rounding");
    EXPECT_EQ(types[3].precisionBits, 0);
    EXPECT_EQ(types[4].precisionBits, 113);
    EXPECT_EQ(types[5].decimalDigits, 71);
    EXPECT_EQ(types[6].decimalDigits, 147);
    EXPECT_EQ(types[6].note, "software, no subnormals");
}
