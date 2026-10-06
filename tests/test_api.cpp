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
