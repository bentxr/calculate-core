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
const FloatFormatInfo& entry(FloatFormat format) {
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

TEST(Inspect, ATenthInBinary32) {
    const FloatInspection r = inspectDecimal(entryOf(NumberType::Float), "0.1");
    ASSERT_FALSE(r.error);
    EXPECT_EQ(r.format, FloatFormat::Binary32);
    const FloatBits& s = r.stored;
    EXPECT_EQ(s.valueClass, FloatClass::Normal);
    EXPECT_FALSE(s.negative);
    EXPECT_EQ(s.sign, "0");
    EXPECT_EQ(s.exponent, "01111011");
    EXPECT_EQ(s.fraction, "10011001100110011001101");
    EXPECT_EQ(s.hex, "3DCCCCCD");
    EXPECT_EQ(s.biasedExponent, 123);
    EXPECT_EQ(s.exponent2, -4);
    EXPECT_EQ(s.significand.digits, "160000002384185791015625");
    EXPECT_EQ(s.significand.exponent10, 0);
    EXPECT_EQ(s.value.digits, "100000001490116119384765625");
    EXPECT_EQ(s.value.exponent10, -1);
    EXPECT_FALSE(r.conversionError.negative);  // stored − typed: rounded up
    EXPECT_EQ(r.conversionError.digits, "1490116119384765625");
    EXPECT_EQ(r.conversionError.exponent10, -9);
    EXPECT_EQ(r.note, "");
}

TEST(Inspect, ATenthInDoubleIsTheCalculatorsInputError) {
    const FloatInspection r = inspectDecimal(entryOf(NumberType::Double), "0.1");
    EXPECT_EQ(r.stored.hex, "3FB999999999999A");
    EXPECT_EQ(r.stored.exponent, "01111111011");
    EXPECT_EQ(r.conversionError.digits, "55511151231257827021181583404541015625");
    EXPECT_EQ(r.conversionError.exponent10, -18);
    EXPECT_EQ(evaluate("0.1").inputError, "5.6e-18");  // the same figure, rounded for the error line
}

TEST(Inspect, DisplayOnlyFormatsAndSigns) {
    const FloatInspection half = inspectDecimal(entry(FloatFormat::Binary16), "-0.1");
    EXPECT_EQ(half.stored.hex, "AE66");
    EXPECT_TRUE(half.stored.negative);
    EXPECT_EQ(half.stored.value.digits, "999755859375");
    EXPECT_EQ(half.stored.value.exponent10, -2);
    EXPECT_FALSE(half.conversionError.negative);  // −0.0999755859375 − (−0.1) > 0
    EXPECT_EQ(half.conversionError.digits, "244140625");
    EXPECT_EQ(half.conversionError.exponent10, -5);
    EXPECT_EQ(inspectDecimal(entry(FloatFormat::Bfloat16), "0.1").stored.hex, "3DCD");
    EXPECT_EQ(inspectDecimal(entryOf(NumberType::Double), "-0").stored.hex, "8000000000000000");
    EXPECT_EQ(inspectDecimal(entryOf(NumberType::Double), "−2.5e−1").stored.hex, "BFD0000000000000");
}

TEST(Inspect, OverflowUnderflowInfinityAndNaN) {
    const FloatInspection big = inspectDecimal(entryOf(NumberType::Float), "1e39");
    EXPECT_EQ(big.stored.valueClass, FloatClass::Infinite);
    EXPECT_EQ(big.stored.hex, "7F800000");
    EXPECT_EQ(big.note, "overflow");
    EXPECT_EQ(big.conversionError.digits, "");  // infinitely far
    const FloatInspection tiny = inspectDecimal(entryOf(NumberType::Float), "1e-50");
    EXPECT_EQ(tiny.stored.valueClass, FloatClass::Zero);
    EXPECT_EQ(tiny.note, "underflow");
    EXPECT_TRUE(tiny.conversionError.negative);
    EXPECT_EQ(tiny.conversionError.digits, "1");
    EXPECT_EQ(tiny.conversionError.exponent10, -50);
    const FloatInspection flushed = inspectDecimal(entryOf(NumberType::Binary128), "1e-4940");
    EXPECT_EQ(flushed.stored.valueClass, FloatClass::Zero);  // IEEE binary128 would keep a subnormal
    EXPECT_EQ(flushed.note, "underflow");
    EXPECT_EQ(inspectDecimal(entryOf(NumberType::Double), "inf").stored.hex, "7FF0000000000000");
    EXPECT_EQ(inspectDecimal(entryOf(NumberType::Double), "-∞").stored.hex, "FFF0000000000000");
    const FloatInspection nan = inspectDecimal(entryOf(NumberType::Double), "nan");
    EXPECT_EQ(nan.stored.valueClass, FloatClass::QuietNaN);
    EXPECT_EQ(nan.stored.hex, "7FF8000000000000");
}

TEST(Inspect, UnreadableText) {
    const FloatInspection r = inspectDecimal(entryOf(NumberType::Double), "0.1.2");
    ASSERT_TRUE(r.error);
    EXPECT_EQ(r.error->code, ErrorCode::InvalidNumber);
    EXPECT_TRUE(inspectDecimal(entryOf(NumberType::Double), "").error);
    EXPECT_EQ(inspectDecimal(entryOf(NumberType::Double), "1e9999999").note, "overflow");  // decided without building the number
}
