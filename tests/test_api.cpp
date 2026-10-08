// Black-box tests: only the public header.
#include <calculate-core/calculate-core.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <map>
#include <set>

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

namespace {

Options as(NumberType type, AngleUnit angle = AngleUnit::Radians) {
    Options o;
    o.type = type;
    o.angle = angle;
    return o;
}

}  // namespace

TEST(Api, ExactResultsAreFractions) {
    const Result r = evaluate("1/3", as(NumberType::Exact));
    ASSERT_FALSE(r.error);
    ASSERT_TRUE(r.exact);
    EXPECT_EQ(r.exact->numerator, "1");
    EXPECT_EQ(r.exact->denominator, "3");
    EXPECT_TRUE(r.exact->hasDecimal);
    EXPECT_EQ(r.exact->repeatingDigits, "3");
    EXPECT_TRUE(r.value.digits.empty());
    EXPECT_EQ(r.bound, "0");
    EXPECT_EQ(r.measured, "0");
}

TEST(Api, ErrorsCarryCodesAndSpans) {
    const Result division = evaluate("1 + 1/0");
    ASSERT_TRUE(division.error);
    EXPECT_EQ(division.error->code, ErrorCode::DivisionByZero);
    EXPECT_EQ(division.error->begin, 4u);
    EXPECT_EQ(division.error->end, 7u);
    EXPECT_EQ(evaluate("sin(1)", as(NumberType::Exact)).error->code, ErrorCode::NotAvailableInExact);
    EXPECT_EQ(evaluate("2π").error->code, ErrorCode::MissingOperator);
    EXPECT_EQ(evaluate("1e400").error->code, ErrorCode::LiteralOutOfRange);
    EXPECT_FALSE(evaluate("1e400", as(NumberType::Binary128)).error);
}

TEST(Api, AnglesAndOptions) {
    const Result r = evaluate("sin(30)", as(NumberType::Double, AngleUnit::Degrees));
    ASSERT_FALSE(r.error);
    EXPECT_EQ(r.value.digits.substr(0, 3), "499");  // 0.4999999999999999... : pi/6 is not exact
    Options allow;
    allow.allowUncertainDiscreteArguments = true;
    EXPECT_EQ(evaluate("(0.1*30)!").error->code, ErrorCode::UncertainDiscreteArgument);
    const Result six = evaluate("(0.1*30)!", allow);
    ASSERT_FALSE(six.error);
    EXPECT_EQ(six.value.digits, "6");
    EXPECT_FALSE(six.boundComplete);
    std::atomic<bool> cancel{true};
    Options cancelled;
    cancelled.cancel = &cancel;
    EXPECT_EQ(evaluate("1+2", cancelled).error->code, ErrorCode::Cancelled);
}

TEST(Api, EveryTypeEvaluatesTheHeadlineCases) {
    for (const TypeInfo& t : numberTypes()) {
        for (const char* text : {"0.1 + 0.2", "1e16 + 1 - 1e16", "sin(1e10)"}) {
            const Result r = evaluate(text, as(t.type));
            if (t.type == NumberType::Exact && std::string(text) == "sin(1e10)") {
                EXPECT_TRUE(r.error);
                continue;
            }
            ASSERT_FALSE(r.error) << t.label << ": " << text;
            EXPECT_TRUE(r.measurementReliable) << t.label << ": " << text;
        }
    }
}

TEST(Api, ACalculatorSizedExpressionIsFastInEveryType) {
    const char* text = "sin(1)+cos(2)+ln(3)+exp(4)+atan(5)+sqrt(6)+sinh(0.7)+asin(0.3)+tan(0.4)+cbrt(9)";
    for (const TypeInfo& t : numberTypes()) {
        if (t.type == NumberType::Exact) continue;
        const auto start = std::chrono::steady_clock::now();
        const Result r = evaluate(text, as(t.type));
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        ASSERT_FALSE(r.error);
        EXPECT_LT(ms, 1000.0) << t.label;  // the budget is 250 ms natively; this guards against regressions
        RecordProperty(t.label + "_ms", static_cast<int>(ms));
    }
}

TEST(Api, FunctionsForKeypads) {
    const std::vector<FunctionDescription> list = functions();
    auto find = [&](const std::string& name) {
        for (const FunctionDescription& f : list) if (f.name == name) return f;
        return FunctionDescription{"", 0, 0, false};
    };
    EXPECT_FALSE(find("sin").exact);
    EXPECT_TRUE(find("sqrt").exact);
    EXPECT_EQ(find("log").minArgs, 1);
    EXPECT_EQ(find("log").maxArgs, 2);
    EXPECT_EQ(find("mean").maxArgs, -1);
    EXPECT_EQ(find("var").minArgs, 2);
    EXPECT_FALSE(find("pi").exact);
}

TEST(Session, AnsIsThePreviousExpression) {
    Session s;
    EXPECT_EQ(s.evaluate("Ans").error->code, ErrorCode::UnknownName);
    EXPECT_FALSE(s.evaluate("1 + 2").error);
    EXPECT_EQ(s.answer(), "1 + 2");
    const Result r = s.evaluate("Ans*2");
    EXPECT_EQ(r.value.digits, "6");
    EXPECT_EQ(r.expression, "(1 + 2)*2");
    EXPECT_EQ(s.answer(), "(1 + 2)*2");
    EXPECT_TRUE(s.evaluate("1/0").error);
    EXPECT_EQ(s.answer(), "(1 + 2)*2");  // errors change nothing
    EXPECT_EQ(s.history().size(), 2u);
}

TEST(Session, AnsIsRecomputedInTheNewType) {
    Session s;
    s.evaluate("0.1 + 0.2");
    const Result exact = s.evaluate("Ans", as(NumberType::Exact));
    ASSERT_TRUE(exact.exact);
    EXPECT_EQ(exact.exact->numerator, "3");
    EXPECT_EQ(exact.exact->denominator, "10");
}

TEST(Session, Memory) {
    Session s;
    EXPECT_FALSE(s.memoryAdd());
    s.evaluate("2");
    EXPECT_TRUE(s.memoryAdd());
    EXPECT_EQ(s.memory(), "2");
    s.evaluate("3");
    EXPECT_TRUE(s.memoryAdd());
    s.evaluate("10");
    EXPECT_TRUE(s.memorySubtract());
    EXPECT_EQ(s.memory(), "2+(3)-(10)");
    EXPECT_EQ(s.evaluate("M").value.digits, "5");
    EXPECT_TRUE(s.evaluate("M").value.negative);
    s.memoryClear();
    EXPECT_EQ(s.evaluate("M").error->code, ErrorCode::UnknownName);
    s.clearHistory();
    EXPECT_TRUE(s.history().empty());
}

TEST(Api, AValueAsSmallAsItsErrorStillHasABound) {
    // 1e-17 + 1 - 1 is 0 in double, with an error of about 1e-17: its square is not exactly 0.
    for (const char* text : {"(1e-17+1-1)^2", "(1e-17+1-1)²", "(1e-17+1-1)*(1e-17+1-1)"}) {
        const Result r = evaluate(text);
        ASSERT_FALSE(r.error) << text;
        EXPECT_EQ(r.value.digits, "0") << text;
        EXPECT_NE(r.bound, "0") << text;
        ASSERT_TRUE(r.measuredAvailable) << text;
        EXPECT_GE(std::stod(r.bound), std::stod(r.measured)) << text;
    }
}

TEST(Api, ModuloRefusesArgumentsWhoseErrorReachesAJump) {
    // 0.7 + 0.1 lands just below 0.8: the exact mod(0.8, 0.8) is 0, the computed one almost 0.8.
    const Result r = evaluate("mod(0.7 + 0.1, 0.8)");
    ASSERT_TRUE(r.error);
    EXPECT_EQ(r.error->code, ErrorCode::ArgumentNearJump);
    EXPECT_EQ(r.error->begin, 0u);
    EXPECT_EQ(r.error->end, 19u);
    EXPECT_NE(r.error->message.find("mod jumps within the error of its arguments"), std::string::npos);
    Options allow;
    allow.allowUncertainDiscreteArguments = true;
    const Result anyway = evaluate("mod(0.7 + 0.1, 0.8)", allow);
    ASSERT_FALSE(anyway.error);
    EXPECT_FALSE(anyway.boundComplete);
    const Result far = evaluate("mod(0.1*3, 1)");  // uncertain, but far from 0 and 1
    ASSERT_FALSE(far.error);
    EXPECT_TRUE(far.boundComplete);
    EXPECT_FALSE(evaluate("mod(7.5, 2)").error);         // exactly known
    EXPECT_FALSE(evaluate("mod(-1e-30, 1)").error);      // near 0, where a truncated remainder is continuous
    EXPECT_FALSE(evaluate("mod(-0.1*3, 1)").error);
    EXPECT_FALSE(evaluate("mod(0.7 + 0.1, 0.8)", as(NumberType::Exact)).error);  // no error to reach anything
    EXPECT_EQ(evaluate("mod(-7, 3)").value.digits, "1");  // still truncated: -1
    EXPECT_TRUE(evaluate("mod(-7, 3)").value.negative);
}

TEST(Api, AnExactPowerTooLargeToWriteDownIsAnOverflow) {
    const Result r = evaluate("0.7^nPr(12, 12)", as(NumberType::Exact));
    ASSERT_TRUE(r.error);
    EXPECT_EQ(r.error->code, ErrorCode::Overflow);
    EXPECT_FALSE(evaluate("0.7^nPr(12, 12)").error);  // in double it underflows to 0, at once
}

TEST(Api, AnArgumentWhoseErrorReachesAnEdgeIsRefused) {
    for (const char* text : {"sqrt(0.1+0.2-0.3)", "cbrt(0.1+0.2-0.3)", "ln(0.1+0.2-0.3)", "log(0.1+0.2-0.3)",
                             "1/(0.1+0.2-0.3)", "(0.1+0.2-0.3)^0.5", "(0.1+0.2-0.3)^-1", "root(0.1+0.2-0.3, 3)",
                             "tan(pi/2)", "asin(0.1*3+0.7)", "acos(0.1*3+0.7)", "acosh(1.1-0.1)", "log(8, 0.1+0.2-0.3)",
                             "mod(5, 0.1+0.2-0.3)"}) {
        const Result r = evaluate(text);
        ASSERT_TRUE(r.error) << text;
        EXPECT_EQ(r.error->code, ErrorCode::ArgumentNearEdge) << text;
    }
    const Result s = evaluate("sqrt(0.1+0.2-0.3)");
    EXPECT_EQ(s.error->begin, 0u);
    EXPECT_EQ(s.error->end, 17u);
    EXPECT_EQ(s.error->message, "sqrt is not defined or not smooth within the error of its argument; "
                                "its argument carries an error of up to 5.6e-17");
    EXPECT_EQ(evaluate("1/(0.1+0.2-0.3)").error->message.rfind("÷ is not defined", 0), 0u);  // operators by their sign
    EXPECT_EQ(evaluate("tan(90)", as(NumberType::Double, AngleUnit::Degrees)).error->code, ErrorCode::ArgumentNearEdge);
    // Far from every edge, or exactly known: computed as before.
    for (const char* text : {"sqrt(2)", "sqrt(0)", "1/3", "ln(1e-300)", "tan(1)", "asin(1)", "acos(0.5)",
                             "(0.1+0.2-0.3)^2", "0^2", "(0.1+0.2)^0.5", "1/(0.1+0.2)", "root(-8, 3)", "atanh(0.5)"})
        EXPECT_FALSE(evaluate(text).error) << text;
    Options allow;
    allow.allowUncertainDiscreteArguments = true;
    const Result anyway = evaluate("sqrt(0.1+0.2-0.3)", allow);
    ASSERT_FALSE(anyway.error);
    EXPECT_FALSE(anyway.boundComplete);
    EXPECT_FALSE(evaluate("sqrt(0.1+0.2-0.3)", as(NumberType::Exact)).error);  // exactly 0
}

TEST(Api, ZeroToAPowerJumpsAtZero) {
    const Result r = evaluate("0^(0.1+0.2-0.3)");
    ASSERT_TRUE(r.error);
    EXPECT_EQ(r.error->code, ErrorCode::ArgumentNearJump);
    EXPECT_FALSE(evaluate("0^(0.1+0.2)").error);  // far from 0
    EXPECT_FALSE(evaluate("0^0").error);          // exactly 0: 1
}

TEST(Api, ANegativeBaseNeedsAnExactlyKnownExponent) {
    for (const char* text : {"(-2)^3.00000000000000001", "(-2)^(0.1*30)", "root(-8, 3.00000000000000001)", "root(-8, 0.1*30)"}) {
        const Result r = evaluate(text);
        ASSERT_TRUE(r.error) << text;
        EXPECT_EQ(r.error->code, ErrorCode::UncertainDiscreteArgument) << text;
    }
    EXPECT_NE(evaluate("(-2)^(0.1*30)").error->message.find("needs an exactly known exponent when its base is negative"),
              std::string::npos);
    EXPECT_EQ(evaluate("(-2)^3").value.digits, "8");  // exact exponents: as before
    EXPECT_FALSE(evaluate("root(-8, 3)").error);
    EXPECT_FALSE(evaluate("2^(0.1*30)").error);       // a positive base is smooth in y
    Options allow;
    allow.allowUncertainDiscreteArguments = true;
    EXPECT_FALSE(evaluate("(-2)^(0.1*30)", allow).boundComplete);
}

TEST(Api, AnUnboundedErrorUpstreamIsRefusedAtOnce) {
    const char* text = "((1e16²)^(abs(-1)-(√(0.1+0.2-0.3))))";
    const Result r = evaluate(text);
    ASSERT_TRUE(r.error);
    EXPECT_EQ(r.error->code, ErrorCode::ArgumentNearEdge);
    Options allow;
    allow.allowUncertainDiscreteArguments = true;
    const Result anyway = evaluate(text, allow);
    ASSERT_FALSE(anyway.error);
    EXPECT_EQ(anyway.bound, "inf");
    EXPECT_FALSE(anyway.boundComplete);
}

TEST(Api, AMedianCountsEveryArgumentThatCanBeTheMedian) {
    const Result r = evaluate("median(0, 1, 0.5+(0.1+0.2-0.3)*1e16)");
    ASSERT_FALSE(r.error);
    ASSERT_TRUE(r.measuredAvailable);
    EXPECT_GE(std::stod(r.bound), std::stod(r.measured));  // measured 0.5
    const Result first = evaluate("median(0.5+(0.1+0.2-0.3)*1e16, 0, 1)");  // the uncertain argument first
    ASSERT_TRUE(first.measuredAvailable);
    EXPECT_GE(std::stod(first.bound), std::stod(first.measured));
    EXPECT_EQ(evaluate("median(1, 2, 3)").bound, "0");
    EXPECT_EQ(evaluate("median(0.1, 5, 9)").bound, "0");  // 0.1's error cannot reach 5
    const Result two = evaluate("median(0.1, 0.2)");     // even count: the mean of the two
    EXPECT_EQ(two.bound, evaluate("(0.1+0.2)/2").bound);
}

TEST(Api, AnArgumentTooLargeToReduceIsAnError) {
    const Result r = evaluate("sin(1e4000)", as(NumberType::Binary128));
    ASSERT_TRUE(r.error);
    EXPECT_EQ(r.error->code, ErrorCode::ArgumentTooLarge);
    EXPECT_EQ(r.error->message, "The arguments of sin are too large to compute accurately");
}

TEST(Api, SemicolonsInCalls) {
    EXPECT_EQ(evaluate("nCr(5; 2)").value.digits, "1");  // 10
    EXPECT_EQ(evaluate("nCr(5; 2)").value.exponent10, 1);
    EXPECT_EQ(evaluate("nCr(5; 2)").expression, "nCr(5; 2)");  // kept as written
}

TEST(Api, LongDoubleStorageFollowsItsFormat) {
    // x87 extended is stored in 80 bits, binary128 in 128, a long double that is a double in 64.
    for (const TypeInfo& t : numberTypes()) {
        if (t.type == NumberType::LongDouble) {
            EXPECT_EQ(t.storageBits, t.precisionBits == 64 ? 80 : t.precisionBits == 113 ? 128 : 64);
        }
    }
}

TEST(Session, APreviewChangesNothing) {
    Session s;
    s.evaluate("1 + 2");
    const Result r = s.preview("Ans*2");
    EXPECT_EQ(r.value.digits, "6");
    EXPECT_EQ(r.expression, "(1 + 2)*2");
    EXPECT_EQ(s.answer(), "1 + 2");
    EXPECT_EQ(s.history().size(), 1u);
    EXPECT_TRUE(s.preview("1/0").error);
    EXPECT_TRUE(s.memoryAdd());
    EXPECT_EQ(s.preview("M + 1").value.digits, "4");
    EXPECT_EQ(s.memory(), "1 + 2");
}

TEST(Api, ExactHasNoSizes) {
    for (const TypeInfo& t : numberTypes()) {
        if (t.type != NumberType::Exact) continue;
        EXPECT_EQ(t.storageBits, 0);
        EXPECT_EQ(t.precisionBits, 0);
        EXPECT_EQ(t.decimalDigits, 0);
    }
}

TEST(Api, EveryFunctionIsListedOnceByName) {
    const std::vector<FunctionDescription> list = functions();
    std::set<std::string> names;
    for (const FunctionDescription& f : list) {
        EXPECT_FALSE(f.name.empty());
        EXPECT_TRUE(names.insert(f.name).second) << f.name;  // log covers both arities
    }
    for (const FunctionDescription& f : list) {
        if (f.name == "var" || f.name == "stdev") {
            EXPECT_EQ(f.maxArgs, -1) << f.name;
        }
    }
}

TEST(Session, MemoryStoreReplacesTheMemoryWithAns) {
    Session s;
    EXPECT_FALSE(s.memoryStore());  // nothing to store yet
    EXPECT_EQ(s.memory(), "");
    s.evaluate("2");
    EXPECT_TRUE(s.memoryAdd());
    s.evaluate("7");
    EXPECT_TRUE(s.memoryStore());
    EXPECT_EQ(s.memory(), "7");  // replaced, not added
    EXPECT_EQ(s.evaluate("M + 1").value.digits, "8");
}

TEST(Api, MessagesUseTheNameAsWritten) {
    EXPECT_EQ(evaluate("log10(-1)").error->message, "log10 is not defined for this argument");
    EXPECT_EQ(evaluate("arcsen(2)").error->message, "arcsen is not defined for this argument");
    EXPECT_EQ(evaluate("sen(1)", as(NumberType::Exact)).error->message,
              "sen is not available in exact arithmetic: its result is irrational");
}

TEST(Api, TheRemaindersAreListedAndExact) {
    int found = 0;
    for (const FunctionDescription& f : functions())
        if ((f.name == "rem" || f.name == "mod") && f.minArgs == 2 && f.maxArgs == 2 && f.exact) ++found;
    EXPECT_EQ(found, 2);
    EXPECT_EQ(evaluate("rem(-7, 3)").value.digits, "1");
    EXPECT_TRUE(evaluate("rem(-7, 3)").value.negative);
}

TEST(Api, AFlooredModuloCanRoundAndSaysSo) {
    // -1e-30 floormod 1 is exactly 1 - 1e-30 (of the stored -1e-30), which double rounds to 1.
    const Result r = evaluate("floormod(-1e-30, 1)");
    ASSERT_FALSE(r.error);
    EXPECT_EQ(r.value.digits, "1");
    EXPECT_EQ(r.value.exponent10, 0);
    EXPECT_EQ(r.roundingError, "1e-30");
    EXPECT_EQ(r.inputError, "8.3e-47");
    const Result exact = evaluate("floormod(-7/2, 3)", as(NumberType::Exact));
    EXPECT_EQ(exact.exact->numerator, "5");
    EXPECT_EQ(exact.exact->denominator, "2");
}

TEST(Api, AFlooredModuloAlsoJumpsAtZero) {
    EXPECT_EQ(evaluate("floormod(0.1+0.2-0.3, 1)").error->code, ErrorCode::ArgumentNearJump);
    EXPECT_FALSE(evaluate("rem(0.1+0.2-0.3, 1)").error);  // a truncated remainder is continuous at 0
    EXPECT_EQ(evaluate("floormod(0.7 + 0.1, 0.8)").error->code, ErrorCode::ArgumentNearJump);
    EXPECT_EQ(evaluate("floormod(5, 0.1+0.2-0.3)").error->code, ErrorCode::ArgumentNearEdge);
    EXPECT_FALSE(evaluate("floormod(-0.3, 1)").error);
}

TEST(Session, ChangingAConventionNeverChangesAnEarlierResult) {
    Session s;
    Options natural;
    natural.conventions.log = Conventions::Log::Natural;
    s.evaluate("log(100)");  // base 10: 2
    EXPECT_EQ(s.answer(), "log10(100)");
    const Result r = s.evaluate("Ans + log(1)", natural);  // Ans keeps its meaning; this log is ln
    EXPECT_EQ(r.value.digits, "2");
    EXPECT_EQ(r.expression, "(log10(100)) + ln(1)");
    EXPECT_EQ(s.history()[0].input, "log(100)");  // the history keeps what was typed
    Options floored;
    floored.conventions.mod = Conventions::Mod::Floored;
    EXPECT_EQ(evaluate("mod(-7, 3)", floored).value.digits, "2");
    EXPECT_FALSE(evaluate("mod(-7, 3)", floored).value.negative);
    EXPECT_EQ(evaluate("mod(-7, 3)").value.digits, "1");  // the default: truncated, -1
}

TEST(Api, PercentagesAddedOrSubtractedUnderEachConvention) {
    Options of;
    of.conventions.percent = Conventions::Percent::OfValue;
    const Result up = evaluate("100 + 10%", of);
    EXPECT_EQ(up.value.digits, "11");
    EXPECT_EQ(up.value.exponent10, 2);
    EXPECT_EQ(up.bound, "0");  // 100 × 10 and ÷ 100 are exact
    EXPECT_EQ(evaluate("100 - 10%", of).value.digits, "9");
    EXPECT_EQ(evaluate("100 × 10%", of).value.exponent10, 1);  // 10
    EXPECT_EQ(evaluate("100 ÷ 10%", of).value.exponent10, 3);  // 1000
    const Result shop = evaluate("19.99 + 21%", of);
    EXPECT_EQ(shop.value.digits, "241878999999999990677679306827485561370849609375");
    EXPECT_TRUE(shop.measurementReliable);
    Options exactOf = of;
    exactOf.type = NumberType::Exact;
    const Result exact = evaluate("19.99 + 21%", exactOf);
    EXPECT_EQ(exact.exact->numerator, "241879");
    EXPECT_EQ(exact.exact->denominator, "10000");
    EXPECT_EQ(evaluate("100 + 10% + 5%", exactOf).exact->numerator, "231");  // 231/2, compounded
    EXPECT_EQ(evaluate("50 - 50%", exactOf).exact->numerator, "25");
    EXPECT_EQ(evaluate("100 + 10%", as(NumberType::Exact)).exact->numerator, "1001");  // the default: 1001/10
}

TEST(Session, APercentageKeepsItsMeaningInAns) {
    Session s;
    Options of;
    of.conventions.percent = Conventions::Percent::OfValue;
    s.evaluate("200 + 10%", of);
    EXPECT_EQ(s.evaluate("Ans").value.digits, "22");  // 220 under the default too
}

// Where an elementary function's value is a known rational (ln 1 = 0, cos 0 = 1, log10 1000 = 3), the computed
// value is checked against it, so a correct kernel claims no error there.
TEST(Api, ElementaryFunctionsHaveNoErrorAtTheirExactPoints) {
    for (const NumberType type : {NumberType::Float, NumberType::Double, NumberType::Binary128}) {
        for (const char* text : {"ln(1)", "log10(1)", "log(1000)", "exp(0)", "sin(0)", "cos(0)", "tan(0)", "asin(0)",
                                 "acos(1)", "atan(0)", "sinh(0)", "cosh(0)", "tanh(0)", "asinh(0)", "acosh(1)", "atanh(0)"}) {
            const Result r = evaluate(text, as(type));
            ASSERT_FALSE(r.error) << text;
            EXPECT_EQ(r.bound, "0") << text;
            EXPECT_EQ(r.trustedDigits, r.value.digits == "0" ? 1 : static_cast<int>(r.value.digits.size())) << text;
        }
    }
    EXPECT_EQ(evaluate("ln(1)").value.digits, "0");
    for (const char* text : {"ln(2)", "log10(0.001)", "sin(1e-300)", "exp(1e-300)", "cos(pi)", "log(1001)"})
        EXPECT_NE(evaluate(text).bound, "0") << text;  // only the exact points
}

TEST(Api, CommentsAreKeptButNotEvaluated) {
    const Result r = evaluate("(5×2)/2 # triangle area");
    ASSERT_FALSE(r.error);
    EXPECT_EQ(r.value.digits, "5");
    EXPECT_EQ(r.comment, "triangle area");
    EXPECT_EQ(r.expression, "(5×2)/2");
    EXPECT_FALSE(r.commentOnly);
    const Result note = evaluate("# shopping list");
    ASSERT_FALSE(note.error);
    EXPECT_TRUE(note.commentOnly);
    EXPECT_EQ(note.comment, "shopping list");
    EXPECT_TRUE(note.value.digits.empty());
}

TEST(Session, CommentsStayInTheHistoryButNotInAns) {
    Session s;
    s.evaluate("1 + 2 # three");
    EXPECT_EQ(s.answer(), "1 + 2");
    EXPECT_EQ(s.evaluate("Ans*2").value.digits, "6");
    EXPECT_FALSE(s.evaluate("# a note").error);
    EXPECT_EQ(s.answer(), "(1 + 2)*2");  // a note changes nothing but the history
    ASSERT_EQ(s.history().size(), 3u);
    EXPECT_EQ(s.history()[0].input, "1 + 2 # three");
    EXPECT_EQ(s.history()[0].result.comment, "three");
    EXPECT_TRUE(s.history()[2].result.commentOnly);
}

TEST(Api, ToFractionShowsTheStoredValueExactly) {
    const Result r = evaluate("0.1 to fraction");
    ASSERT_FALSE(r.error);
    ASSERT_TRUE(r.conversion);
    EXPECT_EQ(r.conversion->target, "fraction");
    EXPECT_EQ(r.conversion->text, "3602879701896397/36028797018963968");
    EXPECT_EQ(r.value.digits, "1000000000000000055511151231257827021181583404541015625");  // the value and report stay
    EXPECT_EQ(r.bound, "5.6e-18");
    EXPECT_EQ(r.expression, "0.1");
    EXPECT_EQ(evaluate("0.1 to fraction", as(NumberType::Float)).conversion->text, "13421773/134217728");
    EXPECT_EQ(evaluate("-1/2 -> fraction").conversion->text, "-1/2");
    EXPECT_EQ(evaluate("6 → fraction", as(NumberType::Exact)).conversion->text, "6");
    EXPECT_EQ(evaluate("1/3 to fraction", as(NumberType::Exact)).conversion->text, "1/3");
    EXPECT_FALSE(evaluate("0.1").conversion);
}

TEST(Api, UnknownTargetsAreErrors) {
    const Result r = evaluate("0.1 to fractoin");
    ASSERT_TRUE(r.error);
    EXPECT_EQ(r.error->code, ErrorCode::UnknownTarget);
    EXPECT_EQ(r.error->begin, 7u);
    EXPECT_EQ(r.error->end, 15u);
    EXPECT_EQ(r.error->message, "Unknown conversion 'fractoin'");
    EXPECT_EQ(evaluate("0.1 to fraction 3").error->code, ErrorCode::UnexpectedToken);
    bool listed = false;
    for (const TargetDescription& t : conversionTargets()) listed = listed || (t.name == "fraction" && !t.summary.empty());
    EXPECT_TRUE(listed);
}

TEST(Session, ATargetAloneConvertsAns) {
    Session s;
    EXPECT_EQ(s.evaluate("to fraction").error->code, ErrorCode::UnknownName);
    s.evaluate("0.1");
    const Result r = s.evaluate("to fraction");
    ASSERT_FALSE(r.error);
    EXPECT_EQ(r.conversion->text, "3602879701896397/36028797018963968");
    EXPECT_EQ(s.answer(), "(0.1)");
    EXPECT_EQ(s.evaluate("Ans + 0 to fraction").conversion->text, "3602879701896397/36028797018963968");
    EXPECT_EQ(s.answer(), "((0.1)) + 0");  // the target is never part of Ans
    EXPECT_EQ(s.history().back().input, "Ans + 0 to fraction");
}

// Mutation survivors of Checkpoint A (Plan 1).
TEST(Api, ARemainderNearZeroSeesTheJumpOnItsNegativeSide) {
    // -0.297 ± 0.83 reaches -1, where rem(x, 1) jumps, but not +1: the nearest whole quotient is 0, so the jump checked
    // must be its lower neighbour.
    EXPECT_EQ(evaluate("rem(-1.13 + (0.1+0.2-0.3)*1.5e16, 1)").error->code, ErrorCode::ArgumentNearJump);
}

TEST(Api, TheLcmWithOneIsExact) {
    const Result r = evaluate("lcm(5, 1)");
    EXPECT_EQ(r.value.digits, "5");
    EXPECT_EQ(r.bound, "0");  // the exact check knows lcm(x, 1) = |x|
}

TEST(Api, SumsAndProducts) {
    EXPECT_EQ(evaluate("sum(x^2; 1; 4)").value.digits, "3");  // 30; arguments separated by ; or ,
    EXPECT_EQ(evaluate("sum(x^2; 1; 4)").value.exponent10, 1);
    EXPECT_EQ(evaluate("sum(x^2, 1, 4)").value.digits, "3");
    EXPECT_EQ(evaluate("sum(x^2; 1; 4)").expression, "sum(x^2; 1; 4)");
    const Result harmonic = evaluate("sum(1/k; 1; 10; k)", as(NumberType::Exact));
    EXPECT_EQ(harmonic.exact->numerator, "7381");
    EXPECT_EQ(harmonic.exact->denominator, "2520");
    EXPECT_EQ(evaluate("product(x, 1, 5)").value.digits, "12");  // 120
    EXPECT_EQ(evaluate("sum(x^2, -2, 2)").value.digits, "1");    // 10
    EXPECT_EQ(evaluate("sum(sum(y, 1, x, y), 1, 3)").value.digits, "1");  // 1 + 3 + 6 = 10
}

TEST(Api, SumAndProductAreListed) {
    int found = 0;
    for (const FunctionDescription& f : functions())
        if ((f.name == "sum" || f.name == "product") && f.minArgs == 3 && f.maxArgs == 4 && f.exact) ++found;
    EXPECT_EQ(found, 2);
}

TEST(Api, ASumIsExactlyItsExpansion) {
    std::string harmonic, sines, alternating;
    for (int k = 1; k <= 10; ++k) {
        const std::string i = std::to_string(k), plus = k > 1 ? "+" : "";
        harmonic += plus + "1/" + i;
        sines += plus + "sin(" + i + ")";
        alternating += plus + "(-1)^" + i + "/" + i;
    }
    const std::vector<std::pair<std::string, std::string>> cases{
        {"sum(1/x, 1, 10)", harmonic}, {"sum(sin(x), 1, 10)", sines}, {"sum((-1)^x/x, 1, 10)", alternating}};
    for (const TypeInfo& t : numberTypes()) {
        for (const auto& [sum, expansion] : cases) {
            const Result a = evaluate(sum, as(t.type));
            const Result b = evaluate(expansion, as(t.type));
            ASSERT_EQ(a.error.has_value(), b.error.has_value()) << t.label << ": " << sum;
            if (a.error) continue;  // sin in Exact
            EXPECT_EQ(a.value.digits, b.value.digits) << t.label << ": " << sum;
            EXPECT_EQ(a.value.exponent10, b.value.exponent10);
            EXPECT_EQ(a.bound, b.bound) << t.label << ": " << sum;
            EXPECT_EQ(a.inputError, b.inputError);
            EXPECT_EQ(a.roundingError, b.roundingError);
            EXPECT_EQ(a.libraryError, b.libraryError);
            EXPECT_EQ(a.measured, b.measured);
            EXPECT_EQ(a.conditionNumber, b.conditionNumber);
            EXPECT_EQ(a.trustedDigits, b.trustedDigits);
            EXPECT_EQ(a.roundingOperations, b.roundingOperations);
            if (a.exact) {
                EXPECT_EQ(a.exact->numerator + "/" + a.exact->denominator, b.exact->numerator + "/" + b.exact->denominator);
            }
        }
    }
}

TEST(Api, TheIndexIsExactUnlessTheTypeCannotHoldIt) {
    const Result factorials = evaluate("sum(x!, 1, 5)");
    ASSERT_FALSE(factorials.error);  // the index is exactly known, so ! accepts it (R.4)
    EXPECT_EQ(factorials.value.digits, "153");
    EXPECT_EQ(evaluate("sum(x, 16777216, 16777218)").inputError, "0");
    EXPECT_NE(evaluate("sum(x, 16777216, 16777218)", as(NumberType::Float)).inputError, "0");  // 16777217 needs 25 bits
}

TEST(Api, CancellationInALongSumShowsInKappa) {
    const double plain = std::stod(evaluate("sum(1/x, 1, 100)").conditionNumber);
    const double alternating = std::stod(evaluate("sum((-1)^x/x, 1, 100)").conditionNumber);
    EXPECT_GT(alternating, plain);
}

// Found by the long fuzz run: the divisor's bound is exactly its true error (both parts are exact), so the jump at
// k = -2500 sits exactly at the end of the error interval; the comparison must not depend on the Ruler's last bit.
TEST(Api, AJumpExactlyAtTheEndOfTheErrorIsSeen) {
    const Result r = evaluate("mod(2.5, (-(0.1%)))", as(NumberType::LongDouble));
    ASSERT_TRUE(r.error);
    EXPECT_EQ(r.error->code, ErrorCode::ArgumentNearJump);
}

TEST(Api, AnEmptyRangeIsANote) {
    const Result r = evaluate("sum(x; 5; 1)");
    ASSERT_FALSE(r.error);
    EXPECT_EQ(r.value.digits, "0");
    ASSERT_EQ(r.warnings.size(), 1u);
    EXPECT_EQ(r.warnings[0].code, WarningCode::EmptyRange);
    EXPECT_EQ(r.warnings[0].message, "sum from 5 to 1 has no terms, so it is 0");
    EXPECT_EQ(r.warnings[0].begin, 0u);
    EXPECT_EQ(r.warnings[0].end, 12u);
    EXPECT_EQ(evaluate("product(x; 2; 1)").warnings[0].message, "product from 2 to 1 has no terms, so it is 1");
    EXPECT_EQ(evaluate("1 + sum(x; 5; 1)").warnings[0].begin, 4u);
    EXPECT_TRUE(evaluate("sum(x; 1; 3)").warnings.empty());
    EXPECT_TRUE(evaluate("1/0").warnings.empty());
    EXPECT_TRUE(evaluate("sum(x; 5; 1)", as(NumberType::Exact)).warnings.size() == 1u);  // in every type
}

TEST(Session, VariablesHoldExpressions) {
    Session s;
    const Result r = s.evaluate("a := 0.1 + 0.2");
    ASSERT_FALSE(r.error);
    EXPECT_EQ(r.assigned, "a");
    EXPECT_EQ(s.variables().at("a"), "0.1 + 0.2");
    EXPECT_EQ(s.answer(), "0.1 + 0.2");
    EXPECT_EQ(s.evaluate("a*10").expression, "(0.1 + 0.2)*10");
    const Result exact = s.evaluate("a", as(NumberType::Exact));  // recomputed in the new type
    EXPECT_EQ(exact.exact->numerator, "3");
    EXPECT_EQ(exact.exact->denominator, "10");
    s.evaluate("b := a*2");
    EXPECT_EQ(s.variables().at("b"), "(0.1 + 0.2)*2");  // names already expanded
    s.evaluate("a := 1");
    EXPECT_EQ(s.evaluate("b").value.digits.substr(0, 4), "6000");  // b keeps its own text: 0.6000000000000000888…
    EXPECT_TRUE(s.forget("a"));
    EXPECT_FALSE(s.forget("a"));
    EXPECT_EQ(s.evaluate("a").error->code, ErrorCode::UnknownName);
    s.evaluate("x := 100");
    EXPECT_EQ(s.evaluate("sum(x, 1, 3)").value.digits, "6");  // the bound variable wins inside the sum
    EXPECT_EQ(s.evaluate("x").value.exponent10, 2);
    EXPECT_EQ(evaluate("c := 2").assigned, "c");  // without a session nothing is stored
    s.clearVariables();
    EXPECT_TRUE(s.variables().empty());
}

TEST(Api, NotationTargetsShowEveryDigitWithTheBar) {
    const Result sci = evaluate("0.1 to sci");
    ASSERT_TRUE(sci.conversion && sci.conversion->parts);
    EXPECT_EQ(sci.conversion->text, "1.000000000000000|055511151231257827021181583404541015625e-1");
    EXPECT_EQ(sci.conversion->parts->trusted, "1.000000000000000");
    EXPECT_EQ(evaluate("123456.789 to eng").conversion->text, "123.4567890000000|04307366907596588134765625e+3");
    EXPECT_EQ(evaluate("1e25 to simple").conversion->text, "1000000000000000|0905969664");
    EXPECT_EQ(evaluate("0.125 to sci").conversion->text, "1.25e-1");  // exact: no bar
    EXPECT_EQ(evaluate("7/3 to sci", as(NumberType::Exact)).conversion->text, "2.(3)e+0");
    EXPECT_EQ(evaluate("1/7 to sci", as(NumberType::Exact)).conversion->text, "1.(428571)e-1");
    EXPECT_EQ(evaluate("1/30 to eng", as(NumberType::Exact)).conversion->text, "33.(3)e-3");
    EXPECT_EQ(evaluate("1/8 to simple", as(NumberType::Exact)).conversion->text, "0.125");
    EXPECT_EQ(evaluate("0.1 to sci 3").error->code, ErrorCode::UnexpectedToken);
}

TEST(Api, AnExactPeriodStartsAsEarlyAsItCan) {
    EXPECT_EQ(evaluate("100/3 to sci", as(NumberType::Exact)).conversion->text, "3.(3)e+1");
    EXPECT_EQ(evaluate("1/13 to sci", as(NumberType::Exact)).conversion->text, "7.(692307)e-2");  // rotated from 0.(076923)
    EXPECT_EQ(evaluate("1/7 to eng", as(NumberType::Exact)).conversion->text, "142.(857142)e-3");
}

TEST(Api, MixedNumbersAndPercentages) {
    EXPECT_EQ(evaluate("7/3 to mixed", as(NumberType::Exact)).conversion->text, "2 + 1/3");
    EXPECT_EQ(evaluate("-7/3 to mixed", as(NumberType::Exact)).conversion->text, "-(2 + 1/3)");
    EXPECT_EQ(evaluate("1/3 to mixed", as(NumberType::Exact)).conversion->text, "1/3");
    EXPECT_EQ(evaluate("6 to mixed", as(NumberType::Exact)).conversion->text, "6");
    EXPECT_EQ(evaluate("2.7 to mixed").conversion->text, "2 + 788129934789837/1125899906842624");  // the stored value
    EXPECT_EQ(evaluate("0.125 to percent").conversion->text, "12.5%");
    const Result tenth = evaluate("0.1 to percent");
    EXPECT_EQ(tenth.conversion->text, "10.00000000000000|055511151231257827021181583404541015625%");
    EXPECT_EQ(tenth.conversion->parts->suffix, "%");
    EXPECT_EQ(evaluate("1/3 to percent", as(NumberType::Exact)).conversion->text, "33.(3)%");
}

TEST(Api, AFixedDenominatorSaysHowFarItIs) {
    const Result r = evaluate("2.7 to 1/3");
    ASSERT_TRUE(r.conversion);
    EXPECT_EQ(r.conversion->target, "1/n");
    EXPECT_EQ(r.conversion->text, "8/3");
    EXPECT_EQ(r.conversion->note, "off by 3.3e-2");
    EXPECT_EQ(evaluate("2.7 to 1/4").conversion->text, "11/4");
    EXPECT_EQ(evaluate("2.7 to 1/4").conversion->note, "off by -5e-2");
    EXPECT_EQ(evaluate("2.5 to 1/2").conversion->note, "");  // exact: nothing to say
    EXPECT_EQ(evaluate("-2.5 to 1/1").conversion->text, "-3/1");  // halves away from zero
    EXPECT_EQ(evaluate("1 to 1/0").error->code, ErrorCode::UnexpectedToken);
    EXPECT_EQ(evaluate("1 to 1/x").error->code, ErrorCode::UnexpectedToken);
    bool listed = false;
    for (const TargetDescription& t : conversionTargets()) listed = listed || t.name == "1/n";
    EXPECT_TRUE(listed);
}

// Mutation survivors (Plan 1, final checkpoint): the sign of a mixed number, and the largest denominator of 1/n.
TEST(Api, MixedNumbersKeepParenthesesForBothParts) {
    EXPECT_EQ(evaluate("-6 to mixed", as(NumberType::Exact)).conversion->text, "-6");
    EXPECT_EQ(evaluate("-1/3 to mixed", as(NumberType::Exact)).conversion->text, "-1/3");
    EXPECT_FALSE(evaluate("1 to 1/1000000000").error);
    EXPECT_EQ(evaluate("1 to 1/1000000001").error->code, ErrorCode::UnexpectedToken);
}

// nCr(n, r) takes the shorter product: over 37 factors its partial products pass 2^53, over 19 they never do.
TEST(Api, ACombinationTakesTheShorterProduct) {
    EXPECT_EQ(evaluate("nCr(56, 37)").bound, "0");
}

TEST(Api, EveryFunctionIsDescribed) {
    const std::vector<std::string> categories = functionCategories();
    for (const FunctionDescription& f : functions()) {
        EXPECT_FALSE(f.title.empty()) << f.name;
        ASSERT_FALSE(f.description.empty()) << f.name;
        EXPECT_EQ(f.description.back(), '.') << f.name;
        EXPECT_EQ(static_cast<int>(f.arguments.size()), f.maxArgs < 0 ? 1 : f.maxArgs) << f.name;
        EXPECT_NE(std::find(categories.begin(), categories.end(), f.category), categories.end()) << f.name;
        EXPECT_EQ(f.example.rfind(f.name, 0), 0u) << f.name << ": " << f.example;  // the example uses the function
        const Result r = evaluate(f.example);
        EXPECT_FALSE(r.error) << f.name << ": " << f.example;
    }
}

TEST(Api, ArgumentsHaveNamesAndKinds) {
    auto find = [](const std::string& name) {
        for (const FunctionDescription& f : functions()) if (f.name == name) return f;
        return FunctionDescription{"", 0, 0, false};
    };
    const FunctionDescription log = find("log");
    ASSERT_EQ(log.arguments.size(), 2u);
    EXPECT_EQ(log.arguments[1].name, "base");
    EXPECT_EQ(log.minArgs, 1);  // so base is optional
    EXPECT_EQ(find("sin").arguments[0].kind, ArgumentKind::Angle);
    EXPECT_EQ(find("nCr").arguments[0].kind, ArgumentKind::Integer);
    EXPECT_EQ(find("atan2").arguments[0].name, "y");
    EXPECT_EQ(find("mean").arguments.size(), 1u);
    EXPECT_EQ(find("gamma").category, "Special functions");
}

TEST(Api, FunctionsListTheirOtherSpellings) {
    auto aliasesOf = [](const std::string& name) {
        for (const FunctionDescription& f : functions()) if (f.name == name) return f.aliases;
        return std::vector<std::string>{};
    };
    const std::vector<std::string> sin = aliasesOf("sin");
    EXPECT_NE(std::find(sin.begin(), sin.end(), "sen"), sin.end());
    const std::vector<std::string> log = aliasesOf("log");
    EXPECT_NE(std::find(log.begin(), log.end(), "log10"), log.end());
    const std::vector<std::string> trunc = aliasesOf("trunc");
    EXPECT_NE(std::find(trunc.begin(), trunc.end(), "int"), trunc.end());
    for (const FunctionDescription& f : functions())
        for (const std::string& alias : f.aliases) EXPECT_FALSE(evaluate(alias + f.example.substr(f.name.size())).error) << alias;
}

TEST(Api, FunctionsWrittenWithOthersListTheirSpellingsToo) {
    std::map<std::string, std::vector<std::string>> aliases;
    for (const FunctionDescription& f : functions()) aliases[f.name] = f.aliases;
    EXPECT_EQ(aliases["csc"], std::vector<std::string>{"cosec"});
    EXPECT_EQ(aliases["acoth"], std::vector<std::string>({"arcoth", "arccotgh"}));
}

TEST(Uncertainty, PlusMinusThroughTheFacade) {
    const Result r = evaluate("5±0.2");
    ASSERT_FALSE(r.error);
    EXPECT_EQ(r.value.digits, "5");
    EXPECT_EQ(r.bound, "0");
    ASSERT_EQ(r.uncertainInputs.size(), 1u);
    EXPECT_EQ(r.uncertainInputs[0].name, "5±0.2");
    EXPECT_EQ(r.uncertainInputs[0].uncertainty, "2e-1");
    EXPECT_EQ(r.uncertainInputs[0].sensitivity, "1e+0");
    EXPECT_EQ(r.uncertainInputs[0].contribution, "2e-1");
    EXPECT_EQ(r.uncertaintyLinear, "2e-1");
    EXPECT_EQ(r.uncertaintyQuadrature, "2e-1");
    EXPECT_EQ(r.uncertaintyRule, UncertaintyRule::Linear);
    EXPECT_TRUE(r.firstOrderChecked);
    EXPECT_TRUE(r.firstOrderReliable);
}

TEST(Uncertainty, TwoInputsLargestFirst) {
    const Result r = evaluate("(3±0.4)*(4±0.3)");
    ASSERT_EQ(r.uncertainInputs.size(), 2u);
    EXPECT_EQ(r.uncertainInputs[0].name, "3±0.4");  // its enclosing parentheses are dropped
    EXPECT_EQ(r.uncertainInputs[0].sensitivity, "4e+0");
    EXPECT_EQ(r.uncertainInputs[0].contribution, "1.6e+0");
    EXPECT_EQ(r.uncertainInputs[1].name, "4±0.3");
    EXPECT_EQ(r.uncertainInputs[1].contribution, "9e-1");
    EXPECT_EQ(r.uncertaintyLinear, "2.5e+0");
    EXPECT_EQ(r.uncertaintyQuadrature, "1.8e+0");
    EXPECT_EQ(r.trustedDigits, 2);                 // 3 * 4 = 12 exactly
    EXPECT_EQ(r.trustedDigitsWithUncertainty, 0);  // 12 ± 2.5
}

TEST(Uncertainty, NoUncertainInputs) {
    const Result r = evaluate("0.1 + 0.2");
    EXPECT_TRUE(r.uncertainInputs.empty());
    EXPECT_EQ(r.uncertaintyLinear, "");
    EXPECT_EQ(r.uncertaintyQuadrature, "");
    EXPECT_FALSE(r.firstOrderChecked);
    EXPECT_EQ(r.trustedDigitsWithUncertainty, r.trustedDigits);
}

TEST(Uncertainty, ARelativeUncertainty) {
    const Result r = evaluate("5±20%");
    ASSERT_EQ(r.uncertainInputs.size(), 1u);
    EXPECT_EQ(r.uncertainInputs[0].name, "5±20%");
    EXPECT_EQ(r.uncertainInputs[0].uncertainty, "1e+0");
}

TEST(Uncertainty, ExactValuesWithAnUncertainty) {
    Options o;
    o.type = NumberType::Exact;
    const Result r = evaluate("1/3±0.1", o);
    ASSERT_FALSE(r.error);
    ASSERT_TRUE(r.exact);
    EXPECT_EQ(r.exact->denominator, "3");
    EXPECT_EQ(r.bound, "0");
    EXPECT_EQ(r.uncertaintyLinear, "1.1e-2");  // ± binds tighter than ÷: 1/(3±0.1), so 0.1/3² = 0.0111
}

TEST(Uncertainty, AFirstOrderWarning) {
    const Result r = evaluate("(0±1)^2");
    EXPECT_TRUE(r.firstOrderChecked);
    EXPECT_FALSE(r.firstOrderReliable);
    EXPECT_EQ(r.uncertaintyLinear, "0");
    EXPECT_EQ(r.firstOrderObserved, "1e+0");
    ASSERT_EQ(r.warnings.size(), 1u);
    EXPECT_EQ(r.warnings[0].code, WarningCode::FirstOrderUnreliable);
    EXPECT_EQ(r.warnings[0].message, "first order unreliable: at the corners the result moved by 1e+0");
    // sqrt(0.05±0.1) would leave sqrt's domain at a corner: the edge check refuses it first.
    EXPECT_EQ(evaluate("sqrt(0.05±0.1)").error->code, ErrorCode::ArgumentNearEdge);
}

TEST(Session, AnsMinusAnsIsCertain) {
    Session s;
    ASSERT_FALSE(s.evaluate("5±0.2").error);
    const Result r = s.evaluate("Ans-Ans");
    ASSERT_EQ(r.uncertainInputs.size(), 1u);
    EXPECT_EQ(r.uncertainInputs[0].name, "Ans");
    EXPECT_EQ(r.uncertaintyLinear, "0");
}

TEST(Uncertainty, DisplayForms) {
    const Result r = evaluate("5±0.2");
    EXPECT_EQ(r.concise, "5.00(20)");
    EXPECT_EQ(r.plusMinus, "5.00 ± 0.20");
    EXPECT_EQ(r.uncertaintyShown.digits, "20");
    EXPECT_EQ(r.uncertaintyShown.exponent10, -1);
    EXPECT_EQ(evaluate("5±20%").concise, "5.0(10)");
    EXPECT_EQ(evaluate("(3±0.4)*(4±0.3)").plusMinus, "12.0 ± 2.5");
    EXPECT_EQ(evaluate("0.1 + 0.2").concise, "0.300000000000000044(44)");  // the bound alone
    Options statistical;
    statistical.uncertaintyRule = UncertaintyRule::Quadrature;
    EXPECT_EQ(evaluate("(3±0.4)*(4±0.3)", statistical).concise, "12.0(18)");
    Options exact;
    exact.type = NumberType::Exact;
    EXPECT_EQ(evaluate("1/3±0.1", exact).concise, "0.333(11)");  // 1/(3±0.1): ± binds tighter than ÷
    EXPECT_EQ(evaluate("1/3", exact).concise, "");  // no error, no uncertainty
    EXPECT_EQ(evaluate("2+2").plusMinus, "");
}

TEST(Uncertainty, ReadPrecision) {
    Options o;
    o.readPrecision = ReadPrecision::Decimals;
    const Result r = evaluate("1.1*3.20", o);
    ASSERT_EQ(r.uncertainInputs.size(), 2u);
    EXPECT_EQ(r.uncertainInputs[0].name, "1.1");
    EXPECT_EQ(r.uncertainInputs[0].contribution, "1.6e-1");  // 3.2 × 0.05
    EXPECT_EQ(r.uncertainInputs[1].name, "3.20");
    EXPECT_EQ(r.uncertainInputs[1].contribution, "5.5e-3");  // 1.1 × 0.005
    EXPECT_EQ(r.uncertaintyLinear, "1.7e-1");
    EXPECT_EQ(r.uncertaintyQuadrature, "1.6e-1");
    EXPECT_EQ(r.concise, "3.52(17)");
    EXPECT_EQ(r.trustedDigits, 15);
    EXPECT_EQ(r.trustedDigitsWithUncertainty, 1);
    o.uncertaintyRule = UncertaintyRule::Quadrature;
    EXPECT_EQ(evaluate("1.1*3.20", o).concise, "3.52(16)");
}

TEST(Units, ResultsCarryTheirUnit) {
    EXPECT_EQ(evaluate("c").unit, "m·s⁻¹");
    EXPECT_EQ(evaluate("2*c").unit, "m·s⁻¹");
    EXPECT_EQ(evaluate("c^2").unit, "m²·s⁻²");
    EXPECT_EQ(evaluate("h*c").unit, "J·m");
    EXPECT_EQ(evaluate("sqrt(G*m_e)").unit, "m^(3/2)·s⁻¹");
    EXPECT_EQ(evaluate("k_B*300").unit, "J·K⁻¹");
    EXPECT_EQ(evaluate("q_e/m_e").unit, "C·kg⁻¹");
    EXPECT_EQ(evaluate("c/c").unit, "");          // dimensionless
    EXPECT_EQ(evaluate("2+2").unit, "");
    EXPECT_EQ(evaluate("alpha").unit, "");
    EXPECT_TRUE(evaluate("c").unitKnown);
    EXPECT_TRUE(evaluate("2+2").unitKnown);
    EXPECT_EQ(evaluate("c+c").unit, "m·s⁻¹");
    EXPECT_EQ(evaluate("mean(c, 2*c)").unit, "m·s⁻¹");  // a lowering follows too
    EXPECT_EQ(evaluate("c^0.5").unit, "m^(1/2)·s^(-1/2)");
    EXPECT_EQ(evaluate("G±1e-15").unit, "m³·kg⁻¹·s⁻²");
}

TEST(Units, MismatchedUnitsGiveNoUnitAndANote) {
    const Result r = evaluate("c + 1");
    ASSERT_FALSE(r.error);  // the value is still computed
    EXPECT_EQ(r.unit, "");
    EXPECT_FALSE(r.unitKnown);
    ASSERT_EQ(r.warnings.size(), 1u);
    EXPECT_EQ(r.warnings[0].code, WarningCode::UnitsDiffer);
    EXPECT_EQ(r.warnings[0].message, "the units of c (m·s⁻¹) and 1 (none) differ");
    EXPECT_EQ(r.warnings[0].begin, 0u);
    EXPECT_EQ(r.warnings[0].end, 5u);
    EXPECT_EQ(evaluate("sin(c)").warnings[0].message, "sin needs a number without a unit; c has m·s⁻¹");
    EXPECT_EQ(evaluate("(c^3)^(1/3)").unit, "");  // the exponent is not exactly known in double: no unit, no note
    EXPECT_TRUE(evaluate("(c^3)^(1/3)").warnings.empty());
    EXPECT_FALSE(evaluate("m_e_MeV*2").unitKnown);  // MeV: unknown, but no note
    EXPECT_TRUE(evaluate("m_e_MeV*2").warnings.empty());
    Options exact;
    exact.type = NumberType::Exact;
    EXPECT_EQ(evaluate("(c^3)^(1/3)", exact).unit, "m·s⁻¹");  // exact there
}
