// The special functions' tests that need no oracle (exact values, refusals, cancellation, the inverse incomplete
// beta at its ends): in the main executable, so they run in the browser build too.
#include "parser.hpp"
#include "functions.hpp"
#include "special.hpp"
#include "accuracy.hpp"

#include <calculate-core/calculate-core.hpp>

#include <gtest/gtest.h>

#include <atomic>

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
        const Result r = evaluate(text);  // in reasonable time: ctest's time limit (tests/CMakeLists.txt)
        ASSERT_FALSE(r.error) << text;
        EXPECT_NE(r.bound, "0") << text;
    }
}

// gammap, gammaq, betainc and betaincinv move one way in each shape parameter, so an uncertain one (here from a
// rounded product, or a typed number read with its precision) bounds the value between its ends, not at infinity.
TEST(Special, AnUncertainShapeParameterHasAFiniteBound) {
    for (const char* text : {"gammap(0.1*25, 1)", "gammaq(0.1*25, 0.1*3)", "betainc(0.1*25, 4.25, 0.375)",
                             "betaincinv(0.1*25, 0.1*3, 0.4)"}) {
        const Result r = evaluate(text);
        ASSERT_FALSE(r.error) << text;
        EXPECT_NE(r.bound, "inf") << text;
        EXPECT_GE(r.trustedDigits, 14) << text;
    }
    Options read;
    read.readPrecision = ReadPrecision::All;  // 2.5 ± 0.05, 1.0 ± 0.05: the uncertainty, through the derivatives
    const Result r = evaluate("gammap(2.5, 1.0)", read);
    ASSERT_FALSE(r.error);
    EXPECT_NE(r.bound, "inf");
    EXPECT_EQ(r.concise, "0.151(23)");
}

namespace {

// Each value is within its claim of the exact one (claim * u * max(|v|, min(), u * scale)), so long double and
// Binary128 must agree within the sum of their claims: 2 claims where long double is binary128 too (the browser
// build), one claim and a sliver where it is narrower. Binary128 holds every long double exactly.
template <class Sample>
void expectLongDoubleAgreesWithBinary128(FunctionId id, Sample sample) {
    using std::abs;
    using L = long double;
    using B = Binary128;
    std::mt19937_64 rng(static_cast<unsigned>(id) + 900);
    const B uL = exactCast<B>(unitRoundoff<L>());
    const B uB = unitRoundoff<B>();
    const double allowed = claimedFactor(id) * (1 + B(uB / uL).convert_to<double>());
    for (int i = 0; i < test::samplesFor<B>(); ++i) {
        const std::vector<L> args = sample(rng);
        std::vector<B> wide;
        for (const L& a : args) wide.push_back(exactCast<B>(a));
        const Applied<L> l = applyFunction<L>(id, args);
        const Applied<B> b = applyFunction<B>(id, wide);
        ASSERT_FALSE(l.error) << "sample " << i;
        ASSERT_FALSE(b.error) << "sample " << i;
        B floor = exactCast<B>((std::numeric_limits<L>::min)());
        for (const B& f : {B(abs(b.value)), B(uL * exactCast<B>(l.scale)), B(uL * b.scale)})
            if (f > floor) floor = f;
        const double error = B(abs(exactCast<B>(l.value) - b.value) / (uL * floor)).convert_to<double>();
        EXPECT_LE(error, allowed) << "sample " << i << ": " << error << " u";
    }
}

}  // namespace

// The browser build's long double is binary128, which Boost.Math (the other special tests' oracle) refuses; the
// library's own Binary128 computes the same functions in software, so it checks long double in every build.
TEST(Special, LongDoubleAgreesWithBinary128WithinTheirClaims) {
    using L = long double;
    using test::logUniform;
    using test::randomSign;
    using test::uniform;
    const auto one = [](double lo, double hi) { return [=](auto& rng) { return std::vector<L>{uniform<L>(rng, lo, hi)}; }; };
    expectLongDoubleAgreesWithBinary128(FunctionId::Gamma, one(-30, 30));
    expectLongDoubleAgreesWithBinary128(FunctionId::Lgamma, one(-20, 30));
    expectLongDoubleAgreesWithBinary128(FunctionId::Lgamma, [](auto& rng) { return std::vector<L>{logUniform<L>(rng, -60, 60)}; });
    expectLongDoubleAgreesWithBinary128(FunctionId::Digamma, one(-20, 30));
    expectLongDoubleAgreesWithBinary128(FunctionId::Trigamma, one(-20, 30));
    expectLongDoubleAgreesWithBinary128(FunctionId::Beta, [](auto& rng) { return std::vector<L>{uniform<L>(rng, 0.1, 30), uniform<L>(rng, 0.1, 30)}; });
    expectLongDoubleAgreesWithBinary128(FunctionId::Erf, one(-6, 6));
    expectLongDoubleAgreesWithBinary128(FunctionId::Erf, [](auto& rng) { return std::vector<L>{randomSign(rng, logUniform<L>(rng, -60, -1))}; });
    expectLongDoubleAgreesWithBinary128(FunctionId::Erfc, one(-3, 100));
    expectLongDoubleAgreesWithBinary128(FunctionId::Erfinv, one(-0.99, 0.99));
    expectLongDoubleAgreesWithBinary128(FunctionId::Erfcinv, one(0.01, 1.99));
    expectLongDoubleAgreesWithBinary128(FunctionId::Erfcinv, [](auto& rng) { return std::vector<L>{logUniform<L>(rng, -200, -2)}; });
    for (FunctionId id : {FunctionId::GammaP, FunctionId::GammaQ, FunctionId::Igamma, FunctionId::GammaInc})
        expectLongDoubleAgreesWithBinary128(id, [](auto& rng) { return std::vector<L>{uniform<L>(rng, 0.05, 20), uniform<L>(rng, 0, 30)}; });
    expectLongDoubleAgreesWithBinary128(FunctionId::Betainc, [](auto& rng) {
        return std::vector<L>{uniform<L>(rng, 0.1, 10), uniform<L>(rng, 0.1, 10), uniform<L>(rng, 0, 1)};
    });
    expectLongDoubleAgreesWithBinary128(FunctionId::Betaincinv, [](auto& rng) {
        return std::vector<L>{uniform<L>(rng, 0.2, 20), uniform<L>(rng, 0.2, 20), uniform<L>(rng, 0.001, 0.999)};
    });
}
