// The special functions' tests that need no oracle (exact values, refusals, cancellation, the inverse incomplete
// beta at its ends): in the main executable, so they run in the browser build too.
#include "parser.hpp"
#include "functions.hpp"
#include "special.hpp"

#include <calculate-core/calculate-core.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>

using namespace calculate_core;
using namespace calculate_core::detail;

TEST(Stirling, CoefficientsAreBernoulliNumbersOverTwoKTimesTwoKMinusOne) {
    const std::vector<Rational>& c = impl::stirlingRationals();
    // python3: Bernoulli numbers by the recurrence sum_{j<=m} C(m+1, j) B_j = 0, then B_2k / (2k (2k-1))
    const Rational expected[] = {Rational(1, 12),     Rational(-1, 360),         Rational(1, 1260),     Rational(-1, 1680),
                                 Rational(1, 1188),   Rational(-691, 360360),    Rational(1, 156),      Rational(-3617, 122400),
                                 Rational(43867, 244188), Rational(-174611, 125400)};
    ASSERT_EQ(c.size(), static_cast<std::size_t>(impl::targetBits<RulerCheck>() / 6 + 4));
    for (std::size_t k = 0; k < 10; ++k) EXPECT_EQ(c[k], expected[k]) << k;
}

TEST(Special, LogGammaIsAlsoLngamma) {
    EXPECT_EQ(parse("lngamma(3)", AngleUnit::Radians).ast.nodes.back().function, FunctionId::Lgamma);
    EXPECT_EQ(evaluate("lgamma(3)", [] { Options o; o.type = NumberType::Exact; return o; }()).error->code,
              ErrorCode::NotAvailableInExact);
}

TEST(Special, GammaOfWholeNumbersIsExact) {
    const Result r = evaluate("gamma(5)");
    ASSERT_FALSE(r.error);
    EXPECT_EQ(r.value.digits, "24");
    EXPECT_EQ(r.bound, "0");
    EXPECT_NE(evaluate("gamma(0.5)").libraryError, "0");
}

TEST(Special, FactorialOfAFractionPointsToGamma) {
    const Result r = evaluate("2.5!");
    ASSERT_TRUE(r.error);
    EXPECT_EQ(r.error->code, ErrorCode::NotAnInteger);
    EXPECT_EQ(r.error->message, "! needs a whole-number argument; for other values use gamma(x + 1)");
}

TEST(Special, IncompleteGammaCanBeCancelledAndHasACeiling) {
    std::atomic<bool> cancel{true};
    EXPECT_EQ(applyFunction<double>(FunctionId::GammaP, {1e9, 1e9}, &cancel).error.value_or(ErrorCode::Overflow), ErrorCode::Cancelled);
    EXPECT_EQ(applyFunction<double>(FunctionId::GammaP, {1e15, 1e15}).error.value_or(ErrorCode::Overflow), ErrorCode::ArgumentTooLarge);
    EXPECT_EQ(evaluate("gammap(1e15, 1e15)").error->message, "The arguments of gammap are too large to compute accurately");
}

TEST(Special, IncompleteBetaOfWholeParametersIsRational) {
    // I_{3/8}(10, 3) = 108591111/68719476736 (python3 fractions)
    const Applied<Ruler> r = applyFunction<Ruler>(FunctionId::Betainc, {Ruler(10), Ruler(3), Ruler(0.375)});
    ASSERT_FALSE(r.error);
    EXPECT_LE(abs(r.value - Ruler(108591111) / Ruler(68719476736)), ldexp(Ruler(108591111) / Ruler(68719476736), -990));
}

TEST(Special, EndToEnd) {
    for (const char* text : {"gamma(0.5)^2", "lgamma(10) - ln(9!)", "erf(1) + erfc(1)", "gammap(2, 3) + gammaq(2, 3)",
                             "betainc(2, 5, 0.3) + betainc(5, 2, 0.7)", "erfinv(erf(0.5))", "beta(2, 3)*12"}) {
        const Result r = evaluate(text);
        ASSERT_FALSE(r.error) << text;
        EXPECT_TRUE(r.measurementReliable) << text;
        EXPECT_TRUE(r.boundComplete) << text;
    }
}

TEST(Special, AnArgumentWhoseErrorReachesAPoleOrAnEdgeIsRefused) {
    for (const char* text : {"gamma(0.1+0.2-0.3)", "lgamma(0.1+0.2-0.3)", "digamma(0.1+0.2-0.3)", "beta(0.1+0.2-0.3, 2)",
                             "erfcinv(0.1+0.2-0.3)", "gammap(0.1+0.2-0.3, 1)", "gammap(0.5, 0.1+0.2-0.3)",
                             "betainc(0.5, 3, 0.1+0.2-0.3)", "betaincinv(2, 3, 0.1*3+0.7)"}) {
        const Result r = evaluate(text);
        ASSERT_TRUE(r.error) << text;
        EXPECT_EQ(r.error->code, ErrorCode::ArgumentNearEdge) << text;
    }
    for (const char* text : {"gamma(0.1+0.2)", "gamma(-2.5)", "betainc(2, 3, 0.25)", "erfinv(0.5)"})
        EXPECT_FALSE(evaluate(text).error) << text;  // far from every edge, or a finite slope there
}

// x below 0 (and, for betainc, above 1) is outside the domain, whatever the slope there: 1 - 30^(1e-17) is negative
// and 30^(1e-17) is above 1, though both round to an end in double. Found by the fuzz.
TEST(Special, AnArgumentWhoseErrorReachesTheEndOfTheDomainIsRefused) {
    for (const char* text : {"gammap(2, 1-30**1e-17)", "gammaq(2, 0.1+0.2-0.3)", "igamma(2, 1-30**1e-17)",
                             "gammainc(2, 1-30**1e-17)", "betainc(1e-17, 30, 30**1e-17)", "betainc(2, 30, 1-30**1e-17)"}) {
        const Result r = evaluate(text);
        ASSERT_TRUE(r.error) << text;
        EXPECT_EQ(r.error->code, ErrorCode::ArgumentNearEdge) << text;
    }
    EXPECT_FALSE(evaluate("gammap(2, 0)").error);  // an exact end is in the domain
    EXPECT_FALSE(evaluate("betainc(2, 3, 1)").error);
}

// The root can lie closer to an end of [0, 1] than the type resolves (here about 1e-3000 from it): the result is that
// end, and the bound is not zero, so it covers the distance. Found by the fuzz (the evaluation never finished). The
// shadows do find such roots, in a few dozen steps (plain bisection and Newton took minutes), also 1e-300 from 1.
TEST(Special, AnInverseIncompleteBetaBeyondTheTypesReachIsAnEnd) {
    for (const auto& [args, end] : {std::pair{std::vector<double>{0.001, 3, 0.001}, 0.0}, {{3, 0.001, 0.999}, 1.0}}) {
        const Applied<double> r = applyFunction<double>(FunctionId::Betaincinv, args);
        ASSERT_FALSE(r.error) << end;
        EXPECT_EQ(r.value, end);
    }
    for (const char* text : {"betaincinv(0.001, 3, 0.001)", "betaincinv(3, 0.001, 0.999)", "betaincinv(3, 0.01, 0.999)",
                             "betaincinv(1e-17, 0.3, 0.3)"}) {
        const auto start = std::chrono::steady_clock::now();
        const Result r = evaluate(text);
        EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(20)) << text;
        ASSERT_FALSE(r.error) << text;
        EXPECT_NE(r.bound, "0") << text;
    }
}
