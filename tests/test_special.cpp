#include "parser.hpp"
#include "special_oracle.hpp"

#include <calculate-core/calculate-core.hpp>

using namespace calculate_core;
using namespace calculate_core::detail;
using std::ldexp;
using test::logUniform;
using test::randomSign;
using test::uniform;

TEST(SpecialOracle, AgreesWithMpfr) {
    using O = Ruler;
    namespace bm = boost::math;
    // MPFR 4.2.2 at 1600 bits (generator in the plan, cycle 2.22).
    const std::pair<O, const char*> cases[] = {
        {bm::tgamma(O(0.5)), "1.77245385090551602729816748334114518279754945612239"},
        {bm::tgamma(O(-2.5)), "-0.945308720482941881225689324448610764158693043265273"},
        {bm::lgamma(O(2.5)), "0.284682870472919159632494669682701924320137695559895"},
        {bm::digamma(O(1)), "-0.577215664901532860606512090082402431042159335939924"},
        {bm::erf(O(0.5)), "0.520499877813046537682746653891964528736451575757964"},
        {bm::erfc(O(3)), "2.20904969985854413727761295823203798477070873992497e-5"},
        {bm::beta(O(1.5), O(2.5)), "0.196349540849362077403915211454968930262323087460944"},
        {bm::gamma_q(O(1.5), O(2.5)), "0.171797144296733135063606652183051499789098236805969"},
        {bm::erf_inv(O(0.5)), "0.476936276204469873381418353643130559808969749059471"},
    };
    for (const auto& [value, reference] : cases)
        EXPECT_LE(abs(value - O(reference)), ldexp(abs(O(reference)), -160)) << reference;
    EXPECT_LE(abs(bm::trigamma(O(1)) - acos(O(-1)) * acos(O(-1)) / 6), ldexp(O(1), -900));  // psi'(1) = pi^2/6
}

TEST(SpecialOracle, IncompleteBetaSeriesMatchesExactValues) {
    using O = Ruler;
    const O tolerance = ldexp(O(1), -900);
    // Integer a, b: I_x(a, b) = sum_{j=a}^{a+b-1} C(a+b-1, j) x^j (1-x)^(a+b-1-j), a rational (python3 fractions).
    EXPECT_LE(abs(test::betaincReference(O(10), O(3), O(0.375)) - O(108591111) / O(68719476736)), tolerance);
    EXPECT_LE(abs(test::betaincReference(O(2), O(3), O(0.5)) - O(11) / O(16)), tolerance);
    EXPECT_LE(abs(test::betaincReference(O(3), O(10), O(0.625)) - (1 - O(108591111) / O(68719476736))), tolerance);
    EXPECT_LE(abs(test::betaincReference(O(1.5), O(2.5), O(0.25)) - O(1) / O(3)), tolerance);  // MPFR: 1/3
    const O x(0.2);  // the arcsine law: I_x(1/2, 1/2) = (2/pi) asin(sqrt(x))
    EXPECT_LE(abs(test::betaincReference(O(0.5), O(0.5), x) - 2 * asin(sqrt(x)) / acos(O(-1))), tolerance);
    const O y = test::betaincReference(O(10), O(3), O(0.375));
    EXPECT_LE(abs(test::betaincinvReference(O(10), O(3), y, O(0.4)) - O(0.375)), tolerance);
}

TEST(Stirling, CoefficientsAreBernoulliNumbersOverTwoKTimesTwoKMinusOne) {
    const std::vector<Rational>& c = impl::stirlingRationals();
    // python3: Bernoulli numbers by the recurrence sum_{j<=m} C(m+1, j) B_j = 0, then B_2k / (2k (2k-1))
    const Rational expected[] = {Rational(1, 12),     Rational(-1, 360),         Rational(1, 1260),     Rational(-1, 1680),
                                 Rational(1, 1188),   Rational(-691, 360360),    Rational(1, 156),      Rational(-3617, 122400),
                                 Rational(43867, 244188), Rational(-174611, 125400)};
    ASSERT_EQ(c.size(), static_cast<std::size_t>(impl::targetBits<RulerCheck>() / 6 + 4));
    for (std::size_t k = 0; k < 10; ++k) EXPECT_EQ(c[k], expected[k]) << k;
}

template <class T>
class SpecialKernelTest : public ::testing::Test {};
TYPED_TEST_SUITE(SpecialKernelTest, test::FloatingTypes, test::TypeNames);

TYPED_TEST(SpecialKernelTest, SineAndCosineOfPiTimesX) {
    using T = TypeParam;
    using O = test::SpecialOracleFor<T>;
    std::mt19937_64 rng(31);
    for (int i = 0; i < test::samplesFor<T>(); ++i) {
        const T x = uniform<T>(rng, -40, 40);
        const auto [s, c] = sinCosPi(x);
        const O pix = acos(O(-1)) * exactCast<O>(x);
        EXPECT_LE(test::errorInUScaled(toValue(s), O(sin(pix)), T(0)), 1.0) << i;
        EXPECT_LE(test::errorInUScaled(toValue(c), O(cos(pix)), T(0)), 1.0) << i;
    }
    EXPECT_EQ(toValue(sinCosPi(T(3)).first), T(0));
    EXPECT_EQ(toValue(sinCosPi(T(2.5)).first), T(1));
    EXPECT_EQ(toValue(sinCosPi(T(-0.5)).first), T(-1));
}

TYPED_TEST(SpecialKernelTest, LogGammaOfPositiveArguments) {
    using T = TypeParam;
    test::expectSpecialWithinClaim<T>(FunctionId::Lgamma, [](auto& rng) { return std::vector<T>{uniform<T>(rng, 0.01, 30)}; });
    test::expectSpecialWithinClaim<T>(FunctionId::Lgamma, [](auto& rng) { return std::vector<T>{logUniform<T>(rng, -60, 60)}; });
    // The values of T nearest the zeros at 1 and 2: only the floor u * scale can hold there.
    test::expectSpecialWithinClaim<T>(FunctionId::Lgamma, [](auto& rng) {
        const T zero = (rng() & 1) ? T(2) : T(1);
        const T step = ldexp(T(1 + static_cast<int>(rng() % 8)), 1 - precisionBits<T>());
        return std::vector<T>{T(zero + randomSign(rng, step))};
    });
    EXPECT_EQ(applyFunction<T>(FunctionId::Lgamma, {T(1)}).value, T(0));
    EXPECT_EQ(applyFunction<T>(FunctionId::Lgamma, {T(2)}).value, T(0));
}

TEST(Special, LogGammaIsAlsoLngamma) {
    EXPECT_EQ(parse("lngamma(3)", AngleUnit::Radians).ast.nodes.back().function, FunctionId::Lgamma);
    EXPECT_EQ(evaluate("lgamma(3)", [] { Options o; o.type = NumberType::Exact; return o; }()).error->code,
              ErrorCode::NotAvailableInExact);
}

TYPED_TEST(SpecialKernelTest, LogGammaOfNegativeArguments) {
    using T = TypeParam;
    test::expectSpecialWithinClaim<T>(FunctionId::Lgamma, [](auto& rng) { return std::vector<T>{uniform<T>(rng, -20, -0.01)}; });
    for (const T& pole : {T(0), T(-1), T(-7)})
        EXPECT_EQ(applyFunction<T>(FunctionId::Lgamma, {pole}).error.value_or(ErrorCode::Cancelled), ErrorCode::DomainError);
}
