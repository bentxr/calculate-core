#include "accuracy.hpp"
#include "ast_builder.hpp"
#include "engine.hpp"
#include "parser.hpp"
#include "test_support.hpp"

using namespace calculate_core;
using namespace calculate_core::detail;
using test::AstBuilder;

namespace {

Ast sum(const char* a, const char* b) {
    AstBuilder builder;
    builder.literal(a) + builder.literal(b);
    return builder.ast();
}

}  // namespace

TEST(Forward, EvaluatesEveryNode) {
    AstBuilder b;
    b.literal("1") + b.literal("2") * b.literal("3");
    const Forward<double> fw = forward<double>(b.ast());
    ASSERT_FALSE(fw.error);
    EXPECT_EQ(fw.values.back(), 7.0);
    EXPECT_EQ(fw.values.size(), 5u);
}

TEST(Forward, ErrorsCarryTheNodeSpan) {
    AstBuilder b;
    b.literal("1") / b.literal("0");
    Ast ast = b.ast();
    ast.nodes.back().span = {2, 3};
    const Forward<double> fw = forward<double>(ast);
    ASSERT_TRUE(fw.error);
    EXPECT_EQ(fw.error->code, ErrorCode::DivisionByZero);
    EXPECT_EQ(fw.error->begin, 2u);
    EXPECT_EQ(fw.error->end, 3u);
    EXPECT_FALSE(fw.error->message.empty());
}

TEST(Forward, LiteralOutOfRange) {
    AstBuilder b;
    b.literal("1e400");
    const Forward<double> fw = forward<double>(b.ast());
    ASSERT_TRUE(fw.error);
    EXPECT_EQ(fw.error->code, ErrorCode::LiteralOutOfRange);
    EXPECT_FALSE(forward<Binary128>(b.ast()).error);  // 1e400 fits in binary128
}

TEST(Forward, ExactLiteralsBeyondTheMaterializationLimitAreOutOfRange) {
    AstBuilder b;
    b.literal("1e1000001");
    const Forward<Rational> fw = forward<Rational>(b.ast());
    ASSERT_TRUE(fw.error);
    EXPECT_EQ(fw.error->code, ErrorCode::LiteralOutOfRange);
}

TEST(Forward, Cancellation) {
    std::atomic<bool> cancel{true};
    const Forward<double> fw = forward<double>(sum("1", "2"), &cancel);
    ASSERT_TRUE(fw.error);
    EXPECT_EQ(fw.error->code, ErrorCode::Cancelled);
}

TEST(LocalErrors, LiteralsAndConstantsCarryTheirRepresentationError) {
    AstBuilder b;
    const auto tenth = b.literal("0.1");
    const auto pi = b.constant(FunctionId::Pi);
    tenth * pi;
    const Forward<double> fw = forward<double>(b.ast());
    const std::vector<Ruler> locals = localErrors<double>(b.ast(), fw);
    EXPECT_EQ(locals[0], fromRational<Ruler>(abs(toRational(0.1) - Rational(1, 10))));
    EXPECT_EQ(locals[1], fromRational<Ruler>(abs(toRational(fw.values[1]) - constantRational(ConstantId::Pi))));
    EXPECT_GT(locals[2], 0);
}

TEST(Adjoints, SumOverEveryPathOfADag) {
    AstBuilder b;
    const auto x = b.literal("3"), y = b.literal("5");
    x * y + x;  // d/dx = y + 1 = 6, d/dy = x = 3
    const Forward<double> fw = forward<double>(b.ast());
    const Adjoints adj = adjoints(b.ast(), nodePartials<double>(b.ast(), fw));
    EXPECT_EQ(adj.signedAdj[0], 6);
    EXPECT_EQ(adj.signedAdj[1], 3);
    EXPECT_EQ(adj.absoluteAdj[0], 6);
}

TEST(Adjoints, AbsoluteAdjointsIgnoreSigns) {
    AstBuilder b;
    const auto x = b.literal("3");
    x - x * b.literal("2");  // d/dx = 1 - 2 = -1, but |1| + |2| = 3
    const Forward<double> fw = forward<double>(b.ast());
    const Adjoints adj = adjoints(b.ast(), nodePartials<double>(b.ast(), fw));
    EXPECT_EQ(adj.signedAdj[0], -1);
    EXPECT_EQ(adj.absoluteAdj[0], 3);
}

TEST(ForwardBounds, ExactResultsHaveZeroBound) {
    AstBuilder b;
    b.literal("5") + b.literal("1");
    const Forward<double> fw = forward<double>(b.ast());
    const auto bounds = forwardBounds(b.ast(), nodePartials<double>(b.ast(), fw), localErrors<double>(b.ast(), fw));
    EXPECT_EQ(bounds.back(), 0);
}

TEST(ForwardBounds, InexactResultsHavePositiveBound) {
    AstBuilder b;
    b.literal("0.1") * b.literal("30");
    const Forward<double> fw = forward<double>(b.ast());
    const auto bounds = forwardBounds(b.ast(), nodePartials<double>(b.ast(), fw), localErrors<double>(b.ast(), fw));
    EXPECT_GT(bounds.back(), 0);
}

namespace {

// Rump's polynomial at a = 77617, b = 33096 (the classic evaluation order).
Ast rump() {
    AstBuilder k;
    const auto a = k.literal("77617"), b = k.literal("33096");
    const auto b2 = b * b, b4 = b2 * b2, b6 = b4 * b2, b8 = b4 * b4, a2 = a * a;
    k.literal("333.75") * b6 + a2 * (k.literal("11") * a2 * b2 - b6 - k.literal("121") * b4 - k.literal("2"))
        + k.literal("5.5") * b8 + a / (k.literal("2") * b);
    return k.ast();
}

}  // namespace

TEST(Agreement, TooLittlePrecisionIsCaught) {
    // Rump's polynomial fools 113 bits (it gives 1.17...) but not 237 bits (-0.827...).
    const Rational at113 = toRational(forward<Binary128>(rump()).values.back());
    const Rational at237 = toRational(forward<Binary256>(rump()).values.back());
    EXPECT_FALSE(agree(at113, at237, scaleByPowerOfTwo(abs(at237), -(53 + 8))));
    const Rational shadow = toRational(forward<Ruler>(rump()).values.back());
    const Rational check = toRational(forward<RulerCheck>(rump()).values.back());
    EXPECT_TRUE(agree(shadow, check, scaleByPowerOfTwo(abs(check), -(489 + 8))));
}

TEST(Agreement, WithinTheTolerance) {
    EXPECT_TRUE(agree(Rational(0), Rational(0), Rational(0)));
    EXPECT_FALSE(agree(Rational(1, 1000), Rational(0), Rational(0)));
    EXPECT_TRUE(agree(Rational(1, 1000), Rational(0), Rational(1, 100)));
}

TEST(Format, ScientificWithTwoSignificantDigits) {
    EXPECT_EQ(formatScientific(fromRational<Ruler>(abs(toRational(0.1 + 0.2) - Rational(3, 10)))), "4.4e-17");
    EXPECT_EQ(formatScientific(Ruler(1)), "1e+0");
    EXPECT_EQ(formatScientific(Ruler(20000000000000001LL)), "2e+16");
    EXPECT_EQ(formatScientific(Ruler(0)), "0");
    EXPECT_EQ(formatScientific(std::numeric_limits<Ruler>::infinity()), "inf");
    EXPECT_EQ(formatScientific(Ruler(9.96)), "1e+1");
    EXPECT_EQ(formatScientific(Ruler(123456), 4), "1.235e+5");
    EXPECT_EQ(formatScientific(Ruler(-0.00012)), "-1.2e-4");
    EXPECT_EQ(formatScientific(ldexp(Ruler(1), -3000000)), "1e-903090");
}

TEST(Format, TrustedDigits) {
    const Ruler value = exactCast<Ruler>(0.1 + 0.2);
    const Ruler error = fromRational<Ruler>(abs(toRational(0.1 + 0.2) - Rational(3, 10)));
    EXPECT_EQ(trustedDigits(value, error, 55), 15);
    EXPECT_EQ(trustedDigits(value, Ruler(0), 55), 55);
    EXPECT_EQ(trustedDigits(Ruler(0), error, 1), 0);
    EXPECT_EQ(trustedDigits(Ruler(1), Ruler(0.5), 10), 0);
    EXPECT_EQ(trustedDigits(Ruler(1), Ruler(0.05), 10), 1);
    EXPECT_EQ(trustedDigits(Ruler(1), Ruler(1e-30), 10), 10);
}

TEST(Report, PointOnePlusPointTwo) {
    const Evaluation<double> ev = evaluate<double>(sum("0.1", "0.2"));
    ASSERT_FALSE(ev.error);
    EXPECT_EQ(ev.value, 0.1 + 0.2);
    const Report& r = ev.report;
    // Each term is rounded into the ruler, then summed: that is how the engine adds them.
    const Ruler input = fromRational<Ruler>(abs(toRational(0.1) - Rational(1, 10)))
                      + fromRational<Ruler>(abs(toRational(0.2) - Rational(2, 10)));
    const Rational rounding = abs(toRational(0.1) + toRational(0.2) - toRational(ev.value));
    EXPECT_EQ(r.input, input);
    EXPECT_EQ(r.rounding, fromRational<Ruler>(rounding));
    EXPECT_EQ(r.library, 0);
    EXPECT_EQ(r.bound, r.input + r.rounding);
    EXPECT_EQ(r.measured, fromRational<Ruler>(abs(toRational(ev.value) - Rational(3, 10))));
    EXPECT_TRUE(test::covers(r.bound, r.measured));
    EXPECT_TRUE(r.measuredAvailable);
    EXPECT_TRUE(r.reliable);
    EXPECT_EQ(r.roundingOperations, 1);
    EXPECT_GE(r.condition, 1);
    EXPECT_LT(r.condition, Ruler(1) + Ruler(1e-15));
}

TEST(Report, ExactOperationsHaveZeroBound) {
    const Evaluation<double> ev = evaluate<double>(sum("2", "2"));
    EXPECT_EQ(ev.report.bound, 0);
    EXPECT_EQ(ev.report.measured, 0);
    EXPECT_EQ(ev.report.roundingOperations, 0);
}

TEST(Report, CatastrophicCancellation) {
    AstBuilder b;
    b.literal("1e16") + b.literal("1") - b.literal("1e16");
    const Evaluation<double> ev = evaluate<double>(b.ast());
    EXPECT_EQ(ev.value, 0.0);
    EXPECT_EQ(ev.report.measured, 1);
    EXPECT_TRUE(test::covers(ev.report.bound, ev.report.measured));
    EXPECT_EQ(ev.report.condition, Ruler(20000000000000001LL));  // (1e16 + 1 + 1e16) / 1
}

namespace {

Rational rumpExact() { return Rational(-54767, 66192); }

}  // namespace

template <class T>
class RumpTest : public ::testing::Test {};
TYPED_TEST_SUITE(RumpTest, test::FloatingTypes, test::TypeNames);

TYPED_TEST(RumpTest, BoundCoversTheMeasuredErrorAndTheShadowIsReliable) {
    using T = TypeParam;
    const Evaluation<T> ev = evaluate<T>(rump());
    ASSERT_FALSE(ev.error);
    EXPECT_TRUE(ev.report.measuredAvailable);
    EXPECT_TRUE(ev.report.reliable);
    EXPECT_TRUE(test::covers(ev.report.bound, ev.report.measured));
    EXPECT_EQ(ev.report.measured, fromRational<Ruler>(abs(toRational(ev.value) - rumpExact())));
}

TEST(Rump, DoubleIsWildlyWrongAndIllConditioned) {
    const Evaluation<double> ev = evaluate<double>(rump());
    EXPECT_GT(ev.report.measured, Ruler(1e20));
    EXPECT_GT(ev.report.condition, Ruler(1e30));
}

TEST(Rump, ExactArithmeticIsExact) {
    const Evaluation<Rational> ev = evaluate<Rational>(rump());
    ASSERT_FALSE(ev.error);
    EXPECT_EQ(ev.value, rumpExact());
    EXPECT_EQ(ev.report.bound, 0);
    EXPECT_EQ(ev.report.measured, 0);
    EXPECT_TRUE(ev.report.reliable);
}

TEST(Discrete, UncertainArgumentsAreRefused) {
    AstBuilder b;
    const auto product = b.literal("0.1") * b.literal("30");  // exactly 3 in double, but not exactly known
    b.apply(FunctionId::Factorial, {product});
    const Evaluation<double> ev = evaluate<double>(b.ast());
    ASSERT_TRUE(ev.error);
    EXPECT_EQ(ev.error->code, ErrorCode::UncertainDiscreteArgument);
    EXPECT_NE(ev.error->message.find("error of up to"), std::string::npos);
}

TEST(Discrete, ProceedingAnywayMarksTheBoundIncomplete) {
    AstBuilder b;
    const auto product = b.literal("0.1") * b.literal("30");
    b.apply(FunctionId::Factorial, {product});
    Options options;
    options.allowUncertainDiscreteArguments = true;
    const Evaluation<double> ev = evaluate<double>(b.ast(), options);
    ASSERT_FALSE(ev.error);
    EXPECT_EQ(ev.value, 6.0);
    EXPECT_FALSE(ev.report.boundComplete);
}

TEST(Discrete, ExactArgumentsPass) {
    AstBuilder b;
    const auto six = b.literal("5") + b.literal("1");
    b.apply(FunctionId::Factorial, {six});
    const Evaluation<double> ev = evaluate<double>(b.ast());
    ASSERT_FALSE(ev.error);
    EXPECT_EQ(ev.value, 720.0);
    EXPECT_TRUE(ev.report.boundComplete);
    EXPECT_EQ(ev.report.bound, 0);
}

TEST(Discrete, ExactTypeNeverRefuses) {
    AstBuilder b;
    const auto product = b.literal("0.1") * b.literal("30");
    b.apply(FunctionId::Factorial, {product});
    const Evaluation<Rational> ev = evaluate<Rational>(b.ast());
    ASSERT_FALSE(ev.error);
    EXPECT_EQ(ev.value, 6);
}

TEST(Report, InfiniteDerivativesTimesZeroErrorsAreZero) {
    AstBuilder b;
    b.apply(FunctionId::Sqrt, {b.literal("0")});
    const Evaluation<double> ev = evaluate<double>(b.ast());
    ASSERT_FALSE(ev.error);
    EXPECT_EQ(ev.report.bound, 0);
    EXPECT_TRUE(isFinite(ev.report.bound));
}

template <class T>
class SineOfTenBillionTest : public ::testing::Test {};
TYPED_TEST_SUITE(SineOfTenBillionTest, test::FloatingTypes, test::TypeNames);

TYPED_TEST(SineOfTenBillionTest, IsAccurateAndHonest) {
    using T = TypeParam;
    AstBuilder b;
    b.apply(FunctionId::Sin, {b.literal("1e10")});
    const Evaluation<T> ev = evaluate<T>(b.ast());
    ASSERT_FALSE(ev.error);
    EXPECT_TRUE(ev.report.reliable);
    EXPECT_TRUE(test::covers(ev.report.bound, ev.report.measured));
    EXPECT_GT(ev.report.library, 0);
    // 1e10 is exact in every floating type, so the only error is sin's own.
    EXPECT_EQ(ev.report.input, 0);
    EXPECT_LE(test::errorInU(ev.value, sin(test::Oracle(10000000000LL))), claimedFactor(FunctionId::Sin));
}

TEST(SineOfTenBillion, IsUnavailableInExactArithmetic) {
    AstBuilder b;
    b.apply(FunctionId::Sin, {b.literal("1e10")});
    const Evaluation<Rational> ev = evaluate<Rational>(b.ast());
    ASSERT_TRUE(ev.error);
    EXPECT_EQ(ev.error->code, ErrorCode::NotAvailableInExact);
}

TEST(Propagation, CarriesAnErrorAsLargeAsItsValue) {
    AstBuilder b;
    const auto x = b.literal("1e-17") + b.literal("1") - b.literal("1");  // 0 in double, off by about 1e-17
    x * x;
    const Forward<double> fw = forward<double>(b.ast());
    ASSERT_EQ(fw.values[x.index], 0);
    const std::vector<Ruler> locals = localErrors<double>(b.ast(), fw);
    const Propagation p = propagate<double>(b.ast(), fw, locals);
    const Ruler bx = p.bounds[x.index];
    EXPECT_GT(bx, 0);
    EXPECT_EQ(p.slopes.back(), (std::vector<Ruler>{Ruler(0), bx}));  // |0|, then |0| + bx
    EXPECT_EQ(p.bounds.back(), bx * bx);
    // The absolute adjoints use the slopes, so the report's parts add up to the bound.
    const Adjoints adj = adjoints(b.ast(), nodePartials<double>(b.ast(), fw), p.slopes);
    Ruler total = 0;
    for (std::size_t i = 0; i < locals.size(); ++i) total += adj.absoluteAdj[i] * locals[i];
    EXPECT_TRUE(test::covers(total, p.bounds.back()) && test::covers(p.bounds.back(), total));
    EXPECT_EQ(adj.signedAdj[x.index], 0);  // the derivative at the computed value is still 0
}

namespace {

// Whether the bound of `text`, evaluated in T, covers its true error: the exact value from rational arithmetic.
template <class T>
::testing::AssertionResult coversExactError(const std::string& text) {
    using std::abs;
    const Parsed parsed = parse(text, AngleUnit::Radians);
    if (parsed.error) return ::testing::AssertionFailure() << text << ": " << parsed.error->message;
    const Evaluation<T> ev = evaluate<T>(parsed.ast);
    if (ev.error && ev.error->code == ErrorCode::Overflow) return ::testing::AssertionSuccess();  // too large for T
    if (ev.error) return ::testing::AssertionFailure() << text << ": " << ev.error->message;
    const Forward<Rational> exact = forward<Rational>(parsed.ast);
    if (exact.error) return ::testing::AssertionFailure() << text << ": no exact value";
    const Ruler error = fromRational<Ruler>(abs(toRational(ev.value) - exact.values.back()));
    if (test::covers(ev.report.bound, error)) return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << text << ": bound " << formatScientific(ev.report.bound, 6) << " < true error "
                                         << formatScientific(error, 6);
}

template <class T>
class BoundTest : public ::testing::Test {};
TYPED_TEST_SUITE(BoundTest, test::FloatingTypes, test::TypeNames);

}  // namespace

// Found by the random-expression property: errors as large as the value, and ties at the u² level.
TYPED_TEST(BoundTest, ArgumentErrorsAreCarriedWhole) {
    for (const char* text : {"(1e-17+1-1)^2", "(1e-17+1-1)*(1e-17+1-1)", "(1e-17+1-1)³", "(1e-17+1-1)²",
                             "1e-300*1e-300", "(-(1e-300*100))³", "(1e-300%)²", "0.7²", "0.7³", "0.3³", "1e-17²",
                             "1e-17*(-1e-17)", "(-(0.3-0.2))²", "((20)!²)³", "1e-300³", "(1e16-3)³", "(0.7²)²",
                             "(0.1*3)^3", "(0.1*3)^-2", "1/(0.7²)", "1/(1e16-3)"})
        EXPECT_TRUE(coversExactError<TypeParam>(text));
}

namespace {

// Whether the bound of `text`, evaluated in T, covers its measured error (reliable: the two reference evaluations
// agree far below it). A result too large for T, or an argument too large to reduce, is an honest refusal.
template <class T>
::testing::AssertionResult coversMeasuredError(const std::string& text, AngleUnit angle = AngleUnit::Radians) {
    const Parsed parsed = parse(text, angle);
    if (parsed.error) return ::testing::AssertionFailure() << text << ": " << parsed.error->message;
    Options options;
    options.angle = angle;
    const Evaluation<T> ev = evaluate<T>(parsed.ast, options);
    if (ev.error && (ev.error->code == ErrorCode::Overflow || ev.error->code == ErrorCode::ArgumentTooLarge))
        return ::testing::AssertionSuccess();
    if (ev.error) return ::testing::AssertionFailure() << text << ": " << ev.error->message;
    const Report& r = ev.report;
    if (!r.measuredAvailable || !r.reliable) return ::testing::AssertionFailure() << text << ": no reliable measurement";
    if (test::covers(r.bound, r.measured)) return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << text << ": bound " << formatScientific(r.bound, 6) << " < measured "
                                         << formatScientific(r.measured, 6);
}

}  // namespace

TYPED_TEST(BoundTest, FunctionsCarryTheirArgumentsErrorAtTheSteepestSlope) {
    for (const char* text : {"sin((12)!³)", "sin((0+1e16)²)", "sin((1e16²)*(100*1))"})
        EXPECT_TRUE(coversMeasuredError<TypeParam>(text, AngleUnit::Degrees));
}

// Built for double, where E is -0.28 against a true 0 (other types compute other values for it).
TEST(Bound, EveryFunctionCarriesItsArgumentsErrorAtTheSteepestSlope) {
    const std::string e = "((0.3-0.1-0.2)*1e16)";
    for (const std::string& text :
         {"exp(" + e + ")", "ln(1-" + e + ")", "log(1-" + e + ")", "sqrt(1-" + e + ")", "cbrt(1-" + e + ")",
          "sinh(1+" + e + ")", "cosh(1+" + e + ")", "tan(1+" + e + ")", "sin(2+" + e + ")", "cos(1+" + e + ")",
          "asin(0.5+" + e + ")", "acos(0.5+" + e + ")", "atan(" + e + ")", "tanh(0.5-" + e + ")",
          "asinh(-0.5+" + e + ")", "acosh(2-" + e + ")", "atanh(0.5+" + e + ")", "root(1-" + e + ", 3)",
          "log(1-" + e + ", 3)", "log(8, 2-" + e + ")"})
        EXPECT_TRUE(coversMeasuredError<double>(text));
}

TEST(Edges, TheArgumentWhoseIntervalReachesThem) {
    using F = FunctionId;
    const Ruler some(0.5);
    EXPECT_EQ(impl::edgeReached(F::Divide, {Rational(1), Rational(0)}, {Ruler(0), some}), 1);
    EXPECT_EQ(impl::edgeReached(F::Divide, {Rational(1), Rational(1)}, {Ruler(0), some}), -1);
    EXPECT_EQ(impl::edgeReached(F::Rem, {Rational(5), Rational(0)}, {Ruler(0), some}), 1);
    EXPECT_EQ(impl::edgeReached(F::LogBase, {Rational(8), Rational(1)}, {Ruler(0), some}), 1);
    EXPECT_EQ(impl::edgeReached(F::LogBase, {Rational(0), Rational(2)}, {some, Ruler(0)}), 0);
    EXPECT_EQ(impl::edgeReached(F::Sqrt, {Rational(0)}, {some}), 0);
    EXPECT_EQ(impl::edgeReached(F::Sqrt, {Rational(1)}, {some}), -1);  // 0.5 to 1.5: clear of 0
    EXPECT_EQ(impl::edgeReached(F::Sin, {Rational(0)}, {some}), -1);   // no edge at all
}

TEST(TrustedDigits, CountTheDecadesBetweenErrorAndValue) {
    // floor(-log10(error / value)): 3/32 is 10^-1.03 and 1/16 is 10^-1.2, so one digit each.
    EXPECT_EQ(trustedDigits(Ruler(1), Ruler(0.09375), 16), 1);
    EXPECT_EQ(trustedDigits(Ruler(1), Ruler(0.0625), 16), 1);
    EXPECT_EQ(trustedDigits(Ruler(1), Ruler(0.5), 16), 0);
    EXPECT_EQ(trustedDigits(Ruler(1), Ruler(0), 16), 16);
}

TEST(Format, PartsInEveryNotation) {
    const DecimalDigits tenth{false, "1000000000000000055511151231257827021181583404541015625", -1};
    const NumberParts sci = formatParts(tenth, 16, Notation::Scientific);
    EXPECT_EQ(sci.trusted, "1.000000000000000");
    EXPECT_EQ(sci.noise, "055511151231257827021181583404541015625");
    EXPECT_EQ(sci.exponent10, -1);
    EXPECT_TRUE(sci.hasExponent);
    const NumberParts simple = formatParts(tenth, 16, Notation::Positional);
    EXPECT_EQ(simple.trusted, "0.1000000000000000");
    EXPECT_FALSE(simple.hasExponent);
    const DecimalDigits big{false, "123456789000000004307366907596588134765625", 5};
    const NumberParts eng = formatParts(big, 16, Notation::Engineering);
    EXPECT_EQ(eng.trusted, "123.4567890000000");
    EXPECT_EQ(eng.noise, "04307366907596588134765625");
    EXPECT_EQ(eng.exponent10, 3);
    const DecimalDigits huge{false, "10000000000000000905969664", 25};
    EXPECT_EQ(formatParts(huge, 16, Notation::Positional).trusted, "1000000000000000");
    EXPECT_EQ(formatParts(huge, 16, Notation::Positional).noise, "0905969664");
    const DecimalDigits small{true, "999999999999999954748111825886258685613938723690807819366455078125", -8};
    const NumberParts tiny = formatParts(small, 16, Notation::Positional);
    EXPECT_TRUE(tiny.negative);
    EXPECT_EQ(tiny.trusted, "0.00000009999999999999999");
    EXPECT_EQ(tiny.noise, "54748111825886258685613938723690807819366455078125");
    EXPECT_EQ(formatParts(DecimalDigits{false, "5", 0}, 1, Notation::Engineering).trusted, "5");  // no point when nothing follows
    EXPECT_EQ(formatParts(DecimalDigits{false, "12345", 4}, 5, Notation::Engineering).trusted, "12.345");
}
