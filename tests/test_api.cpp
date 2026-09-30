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

TEST(Api, TheHeadlineExample) {
    const Result r = evaluate("0.1 + 0.2");
    ASSERT_FALSE(r.error);
    EXPECT_EQ(r.type, NumberType::Double);
    EXPECT_FALSE(r.value.negative);
    EXPECT_EQ(r.value.digits, "3000000000000000444089209850062616169452667236328125");
    EXPECT_EQ(r.value.exponent10, -1);
    EXPECT_FALSE(r.exact);
    EXPECT_EQ(r.trustedDigits, 15);
    EXPECT_EQ(r.trustedDigitsMeasured, 15);
    EXPECT_EQ(r.bound, "4.4e-17");
    EXPECT_EQ(r.inputError, "1.7e-17");
    EXPECT_EQ(r.roundingError, "2.8e-17");
    EXPECT_EQ(r.libraryError, "0");
    EXPECT_EQ(r.measured, "4.4e-17");
    EXPECT_EQ(r.conditionNumber, "1e+0");
    EXPECT_TRUE(r.measuredAvailable);
    EXPECT_TRUE(r.measurementReliable);
    EXPECT_TRUE(r.boundComplete);
    EXPECT_EQ(r.roundingOperations, 1);
    EXPECT_EQ(r.expression, "0.1 + 0.2");
}

TEST(Api, ExactFloatingResultsTrustEveryDigit) {
    const Result r = evaluate("2 + 2");
    EXPECT_EQ(r.value.digits, "4");
    EXPECT_EQ(r.bound, "0");
    EXPECT_EQ(r.trustedDigits, 1);
    EXPECT_EQ(r.roundingOperations, 0);
}
