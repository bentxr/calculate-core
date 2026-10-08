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

TEST(Inspect, NeighboursAndUlp) {
    const FloatInspection r = inspectDecimal(entryOf(NumberType::Double), "0.1");
    ASSERT_TRUE(r.hasNeighbours);
    EXPECT_EQ(r.below.hex, "3FB9999999999999");
    EXPECT_EQ(r.below.value.digits, "9999999999999999167332731531132594682276248931884765625");
    EXPECT_EQ(r.below.value.exponent10, -2);
    EXPECT_EQ(r.above.hex, "3FB999999999999B");
    EXPECT_EQ(r.above.value.digits, "10000000000000001942890293094023945741355419158935546875");
    EXPECT_EQ(r.above.value.exponent10, -1);
    EXPECT_EQ(r.ulpExponent, -56);
    EXPECT_EQ(r.ulp.digits, "1387778780781445675529539585113525390625");
    EXPECT_EQ(r.ulp.exponent10, -17);
}

TEST(Inspect, NeighboursAtTheEdges) {
    const FloatInspection zero = inspectDecimal(entryOf(NumberType::Double), "0");
    EXPECT_EQ(zero.above.hex, "0000000000000001");
    EXPECT_EQ(zero.below.hex, "8000000000000001");
    EXPECT_EQ(zero.above.valueClass, FloatClass::Subnormal);
    EXPECT_EQ(zero.ulpExponent, -1074);
    EXPECT_EQ(zero.above.value.digits.size(), 751u);
    const FloatInspection quadZero = inspectDecimal(entryOf(NumberType::Binary128), "0");
    EXPECT_EQ(quadZero.above.hex, "00010000000000000000000000000000");  // no subnormals: the smallest normal
    EXPECT_EQ(quadZero.ulpExponent, -16382);
    const FloatInspection max = inspectDecimal(entryOf(NumberType::Float), "3.4028234663852885981170418348451692544e38");
    EXPECT_EQ(max.stored.hex, "7F7FFFFF");
    EXPECT_EQ(max.above.valueClass, FloatClass::Infinite);
    EXPECT_EQ(max.above.hex, "7F800000");
    EXPECT_FALSE(inspectDecimal(entryOf(NumberType::Float), "nan").hasNeighbours);
}

TEST(Inspect, ValuesTooLongToWriteOutKeepTheirPowerOfTwo) {
    const FloatInspection r = inspectDecimal(entryOf(NumberType::Binary512), "0");
    EXPECT_EQ(r.above.value.digits, "");  // millions of digits
    EXPECT_EQ(r.above.exponent2, -4194302);
    EXPECT_EQ(r.above.significand.digits, "1");
    EXPECT_EQ(r.ulp.digits, "");
}

TEST(InspectBits, HexadecimalAndBinary) {
    const FloatInspection hex = inspectBits(entryOf(NumberType::Float), "3DCC CCCD", 16);
    ASSERT_FALSE(hex.error);
    EXPECT_EQ(hex.stored.value.digits, "100000001490116119384765625");
    EXPECT_EQ(hex.conversionError.digits, "");  // nothing was converted
    const FloatInspection bin = inspectBits(entryOf(NumberType::Float), "0 01111011 10011001100110011001101", 2);
    EXPECT_EQ(bin.stored.hex, "3DCCCCCD");
    EXPECT_EQ(inspectBits(entryOf(NumberType::Double), "0x3fb999999999999a", 16).stored.hex, "3FB999999999999A");
    EXPECT_EQ(inspectBits(entryOf(NumberType::Float), "1", 16).stored.hex, "00000001");  // leading zeros may be left out
}

TEST(InspectBits, SpecialsAndNoncanonicalPatterns) {
    EXPECT_EQ(inspectBits(entryOf(NumberType::Float), "7F800001", 16).stored.valueClass, FloatClass::SignalingNaN);
    const FloatInspection unnormal = inspectBits(entry(FloatFormat::X87Extended), "3FFF4000000000000000", 16);
    EXPECT_EQ(unnormal.stored.valueClass, FloatClass::Noncanonical);
    EXPECT_EQ(unnormal.stored.note, "unnormal");
    EXPECT_FALSE(unnormal.hasNeighbours);
    const FloatInspection sub = inspectBits(entryOf(NumberType::Binary128), "1", 16);
    EXPECT_EQ(sub.stored.valueClass, FloatClass::Subnormal);
    EXPECT_EQ(sub.note, "no subnormals");  // a valid IEEE pattern this type never produces
}

TEST(InspectBits, Errors) {
    EXPECT_EQ(inspectBits(entryOf(NumberType::Float), "1FFFFFFFF", 16).error->code, ErrorCode::LiteralOutOfRange);  // 33 bits
    EXPECT_EQ(inspectBits(entryOf(NumberType::Float), "3DCG", 16).error->code, ErrorCode::InvalidNumber);
    EXPECT_EQ(inspectBits(entryOf(NumberType::Float), "012", 2).error->code, ErrorCode::InvalidNumber);
    EXPECT_TRUE(inspectBits(entryOf(NumberType::Float), "", 2).error);
}

TEST(Inspect, EveryFloatingResultCarriesItsStoredBits) {
    const Result r = evaluate("0.1 + 0.2");
    ASSERT_TRUE(r.stored);
    EXPECT_EQ(r.stored->format, FloatFormat::Binary64);
    EXPECT_EQ(r.stored->stored.hex, "3FD3333333333334");
    EXPECT_EQ(r.stored->below.value.digits, "299999999999999988897769753748434595763683319091796875");
    EXPECT_EQ(r.stored->above.value.digits, "300000000000000099920072216264088638126850128173828125");
    EXPECT_EQ(r.stored->ulpExponent, -54);
    EXPECT_EQ(r.stored->conversionError.digits, "");
    Options single;
    single.type = NumberType::Float;
    EXPECT_EQ(evaluate("1/3", single).stored->stored.hex, "3EAAAAAB");
    Options exact;
    exact.type = NumberType::Exact;
    EXPECT_FALSE(evaluate("1/3", exact).stored);
    EXPECT_FALSE(evaluate("1/0").stored);
}

TEST(Inspect, TheStoredBitsKeepTheSignOfZero) {
    const Result r = evaluate("-0");
    EXPECT_EQ(r.value.digits, "0");
    EXPECT_EQ(r.stored->stored.hex, "8000000000000000");
}

TEST(Inspect, AZeroInBinary512StaysQuick) {
    Options o;
    o.type = NumberType::Binary512;
    const auto start = std::chrono::steady_clock::now();
    const Result r = evaluate("1 - 1", o);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(2));
    EXPECT_EQ(r.stored->above.exponent2, -4194302);
}

namespace {

std::string field(const Result& r, const std::string& label) {
    for (const ConversionField& f : r.conversion->fields)
        if (f.label == label) return f.value;
    return "(none)";
}

}  // namespace

TEST(FloatTargets, ToFp32) {
    const Result r = evaluate("0.1 to fp32");
    ASSERT_FALSE(r.error);
    ASSERT_TRUE(r.conversion);
    EXPECT_EQ(r.conversion->target, "fp32");
    EXPECT_EQ(r.conversion->text, "0 01111011 10011001100110011001101");
    EXPECT_EQ(field(r, "hex"), "0x3DCCCCCD");
    EXPECT_EQ(field(r, "class"), "normal");
    EXPECT_EQ(field(r, "stored"), "0.100000001490116119384765625");
    EXPECT_EQ(field(r, "error"), "+1.490116119384765625e-9");
    EXPECT_EQ(field(r, "ulp"), "2^-27");
    EXPECT_EQ(field(r, "below"), "0.0999999940395355224609375");
    EXPECT_EQ(field(r, "above"), "0.10000000894069671630859375");
    EXPECT_EQ(field(r, "note"), "(none)");
    EXPECT_EQ(r.value.digits, "1000000000000000055511151231257827021181583404541015625");  // the result itself stays
}

TEST(FloatTargets, TypedNumbersAreConvertedDirectly) {
    Options single;
    single.type = NumberType::Float;
    EXPECT_EQ(field(evaluate("0.1 to fp64", single), "hex"), "0x3FB999999999999A");
    EXPECT_EQ(field(evaluate("0.1 + 0 to fp64", single), "hex"), "0x3FB99999A0000000");  // computed in float first
    EXPECT_EQ(field(evaluate("-0.1 to fp16"), "hex"), "0xAE66");
}

TEST(FloatTargets, EveryFormatAndTheResultsOwn) {
    EXPECT_EQ(field(evaluate("1 to fp16"), "hex"), "0x3C00");
    EXPECT_EQ(field(evaluate("1 to bf16"), "hex"), "0x3F80");
    EXPECT_EQ(field(evaluate("1 to fp80"), "hex"), "0x3FFF8000000000000000");
    EXPECT_EQ(field(evaluate("1 to fp128"), "hex"), "0x3FFF0000000000000000000000000000");
    EXPECT_EQ(field(evaluate("1 to binary32"), "hex"), "0x3F800000");
    EXPECT_EQ(evaluate("-0 to bits").conversion->text, "1 00000000000 " + std::string(52, '0'));
    Options exact;
    exact.type = NumberType::Exact;
    EXPECT_EQ(field(evaluate("1/3 to fp32", exact), "hex"), "0x3EAAAAAB");  // the exact 1/3, rounded once
    EXPECT_EQ(evaluate("1/3 to bits", exact).error->code, ErrorCode::NotAvailableInExact);
    const Result big = evaluate("1e39 to fp32");
    EXPECT_EQ(field(big, "class"), "infinite");
    EXPECT_EQ(field(big, "note"), "overflow");
    bool listed = false;
    for (const auto& t : conversionTargets()) listed = listed || t.name == "fp32";
    EXPECT_TRUE(listed);
}
