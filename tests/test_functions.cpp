#include "accuracy.hpp"
#include "functions.hpp"
#include "test_support.hpp"

#include <set>

using namespace calculate_core;
using namespace calculate_core::detail;

TEST(FunctionInfo, EveryIdHasItsOwnRow) {
    for (int i = 0; i < functionCount; ++i)
        EXPECT_EQ(static_cast<int>(functionInfo(static_cast<FunctionId>(i)).id), i);
}

TEST(FunctionInfo, NamesAreUniqueExceptTheTwoLogarithms) {
    std::multiset<std::string_view> names;
    for (int i = 0; i < functionCount; ++i)
        if (!functionInfo(static_cast<FunctionId>(i)).name.empty())
            names.insert(functionInfo(static_cast<FunctionId>(i)).name);
    for (std::string_view name : names)
        EXPECT_EQ(names.count(name), name == "log" ? 2u : 1u) << name;
}

TEST(FunctionInfo, ContinuityAndExactFlags) {
    for (FunctionId id : {FunctionId::Factorial, FunctionId::Gcd, FunctionId::Lcm, FunctionId::Ncr, FunctionId::Npr})
        EXPECT_EQ(functionInfo(id).continuity, Continuity::Discrete);
    EXPECT_EQ(functionInfo(FunctionId::Mod).continuity, Continuity::Piecewise);
    EXPECT_EQ(functionInfo(FunctionId::Sin).continuity, Continuity::Continuous);
    EXPECT_FALSE(functionInfo(FunctionId::Sin).exact);
    EXPECT_FALSE(functionInfo(FunctionId::Pi).exact);
    EXPECT_TRUE(functionInfo(FunctionId::Sqrt).exact);
    EXPECT_TRUE(functionInfo(FunctionId::Divide).exact);
    EXPECT_EQ(functionInfo(FunctionId::Median).maxArgs, -1);
}

template <class T>
class ApplyTest : public ::testing::Test {};
TYPED_TEST_SUITE(ApplyTest, test::AllTypes, test::TypeNames);

TYPED_TEST(ApplyTest, Arithmetic) {
    using T = TypeParam;
    EXPECT_EQ(applyFunction<T>(FunctionId::Add, {T(3), T(4)}).value, T(7));
    EXPECT_EQ(applyFunction<T>(FunctionId::Subtract, {T(3), T(4)}).value, T(-1));
    EXPECT_EQ(applyFunction<T>(FunctionId::Multiply, {T(3), T(4)}).value, T(12));
    EXPECT_EQ(applyFunction<T>(FunctionId::Divide, {T(12), T(4)}).value, T(3));
    EXPECT_EQ(applyFunction<T>(FunctionId::Negate, {T(3)}).value, T(-3));
    EXPECT_EQ(applyFunction<T>(FunctionId::Percent, {T(50)}).value, T(1) / T(2));
    EXPECT_EQ(applyFunction<T>(FunctionId::Square, {T(-3)}).value, T(9));
    EXPECT_EQ(applyFunction<T>(FunctionId::Cube, {T(-2)}).value, T(-8));
    EXPECT_FALSE(applyFunction<T>(FunctionId::Add, {T(3), T(4)}).error);
}

TYPED_TEST(ApplyTest, DivisionByZeroIsAnErrorNeverAValue) {
    using T = TypeParam;  // for Rational this is the Boost guard rule: Boost would silently return 0
    const Applied<T> r = applyFunction<T>(FunctionId::Divide, {T(1), T(0)});
    ASSERT_TRUE(r.error);
    EXPECT_EQ(*r.error, ErrorCode::DivisionByZero);
}

TYPED_TEST(ApplyTest, ConstantsAreUnavailableInExactArithmetic) {
    using T = TypeParam;
    const Applied<T> pi = applyFunction<T>(FunctionId::Pi, {});
    if constexpr (isExact<T>) {
        ASSERT_TRUE(pi.error);
        EXPECT_EQ(*pi.error, ErrorCode::NotAvailableInExact);
    } else {
        EXPECT_FALSE(pi.error);
        EXPECT_EQ(pi.value, constantValue<T>(ConstantId::Pi));
        EXPECT_EQ(applyFunction<T>(FunctionId::E, {}).value, constantValue<T>(ConstantId::E));
    }
}

template <class T>
class FloatingApplyTest : public ::testing::Test {};
TYPED_TEST_SUITE(FloatingApplyTest, test::FloatingTypes, test::TypeNames);

TYPED_TEST(FloatingApplyTest, OverflowIsAnError) {
    using T = TypeParam;
    const T largest = (std::numeric_limits<T>::max)();
    const Applied<T> r = applyFunction<T>(FunctionId::Multiply, {largest, T(2)});
    ASSERT_TRUE(r.error);
    EXPECT_EQ(*r.error, ErrorCode::Overflow);
}

TEST(Partials, ArithmeticRules) {
    const std::vector<Ruler> d = partials<Ruler>(FunctionId::Divide, {Ruler(1), Ruler(4)}, Ruler(0.25));
    EXPECT_EQ(d, (std::vector<Ruler>{Ruler(0.25), Ruler(-0.0625)}));
    EXPECT_EQ(partials<Ruler>(FunctionId::Multiply, {Ruler(3), Ruler(5)}, Ruler(15)),
              (std::vector<Ruler>{Ruler(5), Ruler(3)}));
    EXPECT_EQ(partials<Ruler>(FunctionId::Subtract, {Ruler(3), Ruler(5)}, Ruler(-2)),
              (std::vector<Ruler>{Ruler(1), Ruler(-1)}));
    EXPECT_EQ(partials<Ruler>(FunctionId::Cube, {Ruler(2)}, Ruler(8)), (std::vector<Ruler>{Ruler(12)}));
}

TEST(LocalError, ArithmeticErrorsAreExact) {
    const double a = 0.1, b = 0.2;
    const Applied<double> sum = applyFunction<double>(FunctionId::Add, {a, b});
    const Rational exact = abs(toRational(a) + toRational(b) - toRational(sum.value));
    EXPECT_GT(exact, 0);
    EXPECT_EQ(localError<double>(FunctionId::Add, {a, b}, sum), fromRational<Ruler>(exact));

    const Applied<double> four = applyFunction<double>(FunctionId::Add, {2.0, 2.0});
    EXPECT_EQ(localError<double>(FunctionId::Add, {2.0, 2.0}, four), 0);

    const Applied<double> third = applyFunction<double>(FunctionId::Divide, {1.0, 3.0});
    EXPECT_EQ(localError<double>(FunctionId::Divide, {1.0, 3.0}, third),
              fromRational<Ruler>(abs(Rational(1, 3) - toRational(third.value))));
}

TEST(LocalError, ExactTypeHasNoLocalError) {
    const Applied<Rational> third = applyFunction<Rational>(FunctionId::Divide, {Rational(1), Rational(3)});
    EXPECT_EQ(localError<Rational>(FunctionId::Divide, {Rational(1), Rational(3)}, third), 0);
}

TEST(LocalError, NegationIsExact) {
    const Applied<double> r = applyFunction<double>(FunctionId::Negate, {0.1});
    EXPECT_EQ(localError<double>(FunctionId::Negate, {0.1}, r), 0);
}

TEST(LocalError, LibraryFunctionsUseTheirClaim) {
    const Applied<double> r = applyFunction<double>(FunctionId::Exp, {0.5});
    const Ruler expected = Ruler(claimedFactor(FunctionId::Exp)) * exactCast<Ruler>(unitRoundoff<double>())
                         * exactCast<Ruler>(r.value);
    EXPECT_EQ(localError<double>(FunctionId::Exp, {0.5}, r), expected);
}

TEST(ExactFunctions, RootsAndPowersThatAreRational) {
    const auto value = [](FunctionId id, std::vector<Rational> args) {
        const Applied<Rational> r = applyFunction<Rational>(id, args);
        EXPECT_FALSE(r.error) << static_cast<int>(id);
        return r.value;
    };
    EXPECT_EQ(value(FunctionId::Sqrt, {Rational(9, 4)}), Rational(3, 2));
    EXPECT_EQ(value(FunctionId::Cbrt, {Rational(-27, 8)}), Rational(-3, 2));
    EXPECT_EQ(value(FunctionId::Root, {Rational(27), Rational(3)}), Rational(3));
    EXPECT_EQ(value(FunctionId::Power, {Rational(4), Rational(1, 2)}), Rational(2));
    EXPECT_EQ(value(FunctionId::Power, {Rational(8), Rational(2, 3)}), Rational(4));
    EXPECT_EQ(value(FunctionId::Power, {Rational(-8), Rational(1, 3)}), Rational(-2));
    EXPECT_EQ(value(FunctionId::Power, {Rational(2), Rational(-3)}), Rational(1, 8));
    EXPECT_EQ(value(FunctionId::Power, {Rational(2, 3), Rational(0)}), Rational(1));
}

TEST(ExactFunctions, IrrationalOrUndefinedResultsAreErrors) {
    const auto code = [](FunctionId id, std::vector<Rational> args) {
        const Applied<Rational> r = applyFunction<Rational>(id, args);
        EXPECT_TRUE(r.error) << static_cast<int>(id);
        return r.error.value_or(ErrorCode::Cancelled);
    };
    EXPECT_EQ(code(FunctionId::Sqrt, {Rational(2)}), ErrorCode::IrrationalResult);
    EXPECT_EQ(code(FunctionId::Power, {Rational(2), Rational(1, 2)}), ErrorCode::IrrationalResult);
    EXPECT_EQ(code(FunctionId::Power, {Rational(-4), Rational(1, 2)}), ErrorCode::DomainError);
    EXPECT_EQ(code(FunctionId::Power, {Rational(0), Rational(-1)}), ErrorCode::DivisionByZero);
    EXPECT_EQ(code(FunctionId::Sin, {Rational(1)}), ErrorCode::NotAvailableInExact);
    EXPECT_EQ(code(FunctionId::Ln, {Rational(1)}), ErrorCode::NotAvailableInExact);
}

TYPED_TEST(ApplyTest, IntegerFunctions) {
    using T = TypeParam;
    const auto value = [](FunctionId id, std::vector<T> args) {
        const Applied<T> r = applyFunction<T>(id, args);
        EXPECT_FALSE(r.error) << static_cast<int>(id);
        return r.value;
    };
    EXPECT_EQ(value(FunctionId::Factorial, {T(5)}), T(120));
    EXPECT_EQ(value(FunctionId::Factorial, {T(0)}), T(1));
    EXPECT_EQ(value(FunctionId::Ncr, {T(5), T(2)}), T(10));
    EXPECT_EQ(value(FunctionId::Ncr, {T(2), T(5)}), T(0));
    EXPECT_EQ(value(FunctionId::Npr, {T(5), T(2)}), T(20));
    EXPECT_EQ(value(FunctionId::Gcd, {T(12), T(18)}), T(6));
    EXPECT_EQ(value(FunctionId::Gcd, {T(0), T(0)}), T(0));
    EXPECT_EQ(value(FunctionId::Lcm, {T(4), T(6)}), T(12));
    EXPECT_EQ(value(FunctionId::Mod, {T(7), T(3)}), T(1));
    EXPECT_EQ(value(FunctionId::Mod, {T(-7), T(3)}), T(-1));  // truncated: the sign of the dividend
    EXPECT_EQ(value(FunctionId::Mod, {T(7), T(-3)}), T(1));
    EXPECT_EQ(value(FunctionId::Mod, {T(11) / T(2), T(2)}), T(3) / T(2));
    EXPECT_EQ(value(FunctionId::Abs, {T(-3)}), T(3));
    EXPECT_EQ(value(FunctionId::Median, {T(3), T(1), T(2)}), T(2));
    EXPECT_EQ(value(FunctionId::Median, {T(4), T(1), T(3), T(2)}), T(5) / T(2));
}

TYPED_TEST(ApplyTest, IntegerFunctionErrors) {
    using T = TypeParam;
    const auto code = [](FunctionId id, std::vector<T> args) {
        const Applied<T> r = applyFunction<T>(id, args);
        EXPECT_TRUE(r.error) << static_cast<int>(id);
        return r.error.value_or(ErrorCode::Cancelled);
    };
    EXPECT_EQ(code(FunctionId::Factorial, {T(7) / T(2)}), ErrorCode::NotAnInteger);
    EXPECT_EQ(code(FunctionId::Factorial, {T(-1)}), ErrorCode::DomainError);
    EXPECT_EQ(code(FunctionId::Gcd, {T(1) / T(2), T(2)}), ErrorCode::NotAnInteger);
    EXPECT_EQ(code(FunctionId::Mod, {T(1), T(0)}), ErrorCode::DivisionByZero);
    EXPECT_EQ(code(FunctionId::Ncr, {T(-5), T(2)}), ErrorCode::DomainError);
}

TEST(IntegerFunctions, ExactTypeHasNoLimit) {
    EXPECT_EQ(applyFunction<Rational>(FunctionId::Ncr, {Rational(100), Rational(50)}).value,
              Rational(Integer("100891344545564193334812497256")));
    EXPECT_EQ(applyFunction<Rational>(FunctionId::Factorial, {Rational(25)}).value,
              Rational(Integer("15511210043330985984000000")));
}

TEST(IntegerFunctions, OverflowAndCountedRoundings) {
    const Applied<double> overflow = applyFunction<double>(FunctionId::Factorial, {171.0});
    ASSERT_TRUE(overflow.error);
    EXPECT_EQ(*overflow.error, ErrorCode::Overflow);
    EXPECT_EQ(applyFunction<double>(FunctionId::Factorial, {10.0}).roundings, 0);  // 10! < 2^53: exact
    EXPECT_GT(applyFunction<double>(FunctionId::Factorial, {25.0}).roundings, 0);
    EXPECT_EQ(applyFunction<double>(FunctionId::Npr, {20.0, 10.0}).value, 670442572800.0);
}

TEST(IntegerFunctions, CancellationStopsLongLoops) {
    std::atomic<bool> cancel{true};
    const Applied<Rational> r = applyFunction<Rational>(FunctionId::Factorial, {Rational(100000)}, &cancel);
    ASSERT_TRUE(r.error);
    EXPECT_EQ(*r.error, ErrorCode::Cancelled);
}

TEST(LocalError, ExactRootsAndIntegerPowersAreChecked) {
    const Applied<double> four = applyFunction<double>(FunctionId::Sqrt, {16.0});
    EXPECT_EQ(localError<double>(FunctionId::Sqrt, {16.0}, four), 0);
    const Applied<double> two = applyFunction<double>(FunctionId::Sqrt, {2.0});
    EXPECT_EQ(localError<double>(FunctionId::Sqrt, {2.0}, two),
              exactCast<Ruler>(unitRoundoff<double>()) * exactCast<Ruler>(two.value));
    const Applied<double> hundred = applyFunction<double>(FunctionId::Power, {10.0, 2.0});
    EXPECT_EQ(localError<double>(FunctionId::Power, {10.0, 2.0}, hundred), 0);
    const Applied<double> square = applyFunction<double>(FunctionId::Power, {0.1, 2.0});
    EXPECT_EQ(localError<double>(FunctionId::Power, {0.1, 2.0}, square),
              fromRational<Ruler>(abs(toRational(0.1) * toRational(0.1) - toRational(square.value))));
    const Applied<double> cube = applyFunction<double>(FunctionId::Cbrt, {-27.0});
    EXPECT_EQ(localError<double>(FunctionId::Cbrt, {-27.0}, cube), 0);
}

TEST(LocalError, CountedFunctions) {
    const Applied<double> r = applyFunction<double>(FunctionId::Factorial, {25.0});
    EXPECT_EQ(localError<double>(FunctionId::Factorial, {25.0}, r),
              Ruler(r.roundings) * exactCast<Ruler>(unitRoundoff<double>()) * exactCast<Ruler>(r.value));
}

namespace {

// Central difference of the oracle at 2017 bits: h^2 truncation, far below the tolerance.
test::Oracle centralDifference(FunctionId id, std::vector<test::Oracle> args, std::size_t k) {
    using O = test::Oracle;
    const O h = ldexp(O(1), -400);
    std::vector<O> up = args, down = args;
    up[k] += h;
    down[k] -= h;
    const O y1 = up.size() > 1 ? up[1] : O(0), y0 = down.size() > 1 ? down[1] : O(0);
    return (test::oracle(id, up[0], y1) - test::oracle(id, down[0], y0)) / (2 * h);
}

}  // namespace

TEST(Partials, EveryRuleMatchesAFiniteDifference) {
    using O = test::Oracle;
    const std::pair<FunctionId, std::vector<double>> cases[] = {
        {FunctionId::Exp, {0.7}},   {FunctionId::Ln, {0.7}},    {FunctionId::Log10, {0.7}},
        {FunctionId::Sin, {0.7}},   {FunctionId::Cos, {0.7}},   {FunctionId::Tan, {0.7}},
        {FunctionId::Asin, {0.3}},  {FunctionId::Acos, {0.3}},  {FunctionId::Atan, {0.7}},
        {FunctionId::Sinh, {0.7}},  {FunctionId::Cosh, {0.7}},  {FunctionId::Tanh, {0.7}},
        {FunctionId::Asinh, {0.7}}, {FunctionId::Acosh, {1.7}}, {FunctionId::Atanh, {0.3}},
        {FunctionId::Sqrt, {0.7}},  {FunctionId::Cbrt, {0.7}},  {FunctionId::Power, {0.7, 2.5}},
        {FunctionId::LogBase, {0.7, 3.0}}, {FunctionId::Root, {0.7, 3.0}}, {FunctionId::Abs, {0.7}},
        {FunctionId::Abs, {-0.7}}};
    for (const auto& [id, point] : cases) {
        std::vector<Ruler> args;
        std::vector<O> oracleArgs;
        for (double p : point) {
            args.push_back(Ruler(p));
            oracleArgs.push_back(O(p));
        }
        const Ruler value = applyFunction<Ruler>(id, args).value;
        const std::vector<Ruler> d = partials<Ruler>(id, args, value);
        ASSERT_EQ(d.size(), args.size());
        for (std::size_t k = 0; k < args.size(); ++k) {
            const O expected = centralDifference(id, oracleArgs, k);
            EXPECT_LE(abs(exactCast<O>(d[k]) - expected), ldexp(O(1), -200) * (abs(expected) + 1))
                << static_cast<int>(id) << " argument " << k;
        }
    }
}

TEST(Partials, SingularPointsAreInfiniteNotNan) {
    const std::vector<Ruler> d = partials<Ruler>(FunctionId::Sqrt, {Ruler(0)}, Ruler(0));
    EXPECT_FALSE(isFinite(d[0]));
    EXPECT_GT(d[0], 0);
}

TEST(Partials, DiscreteFunctionsHaveZeroDerivatives) {
    EXPECT_EQ(partials<Ruler>(FunctionId::Factorial, {Ruler(5)}, Ruler(120)), (std::vector<Ruler>{Ruler(0)}));
    EXPECT_EQ(partials<Ruler>(FunctionId::Median, {Ruler(3), Ruler(1), Ruler(2)}, Ruler(2)),
              (std::vector<Ruler>{Ruler(0), Ruler(0), Ruler(1)}));
    EXPECT_EQ(partials<Ruler>(FunctionId::Mod, {Ruler(7), Ruler(3)}, Ruler(1)), (std::vector<Ruler>{Ruler(1), Ruler(-2)}));
}

namespace {

bool close(const Ruler& a, const Ruler& b) {
    using std::abs;
    using std::ldexp;
    return abs(a - b) <= ldexp(Ruler(1), -800) * (abs(a) > abs(b) ? abs(a) : abs(b));
}

// The points of the box argument k's slope ranges over: the arguments up to k at both ends, the quarters and the
// middle of their intervals; the later ones at their computed values.
std::vector<std::vector<Ruler>> boxPoints(const std::vector<Ruler>& a, const std::vector<Ruler>& b, std::size_t k) {
    std::vector<std::vector<Ruler>> points{a};
    for (std::size_t j = 0; j <= k; ++j) {
        std::vector<std::vector<Ruler>> next;
        for (const std::vector<Ruler>& p : points)
            for (const Ruler& shift : {Ruler(-1), Ruler(-0.5), Ruler(0), Ruler(0.5), Ruler(1)}) {
                std::vector<Ruler> q = p;
                q[j] = a[j] + shift * b[j];
                next.push_back(q);
            }
        points = next;
    }
    return points;
}

// slopes(id, a, b)[k] is at least |d f / d arg_k| at every sampled point of argument k's box (a slope too small
// breaks the bound), at most four times the steepest of them (one far too large loosens it), and, with exact
// arguments, the derivative itself.
void expectSlopesDominate(FunctionId id, const std::vector<double>& point, const std::vector<double>& radius) {
    using std::abs;
    using std::ldexp;
    std::vector<Ruler> a, b;
    for (const double x : point) a.push_back(Ruler(x));
    for (const double r : radius) b.push_back(Ruler(r));
    const bool exactArguments = std::all_of(b.begin(), b.end(), [](const Ruler& r) { return r == 0; });
    const std::vector<Ruler> s = slopes(id, a, b);
    ASSERT_EQ(s.size(), a.size());
    const Ruler slack = ldexp(Ruler(1), -800);
    for (std::size_t k = 0; k < a.size(); ++k) {
        Ruler steepest = 0;
        for (const std::vector<Ruler>& p : boxPoints(a, b, k)) {
            const Applied<Ruler> v = applyFunction<Ruler>(id, p);
            ASSERT_FALSE(v.error) << "argument " << k;  // every box of the tables stays inside the domain
            const Ruler d = abs(partials<Ruler>(id, p, v.value)[k]);
            EXPECT_GE(s[k], d * (1 - slack)) << "argument " << k;
            if (d > steepest) steepest = d;
        }
        EXPECT_LE(s[k], 4 * steepest * (1 + slack)) << "argument " << k;
        if (exactArguments) {
            EXPECT_TRUE(close(s[k], steepest)) << "argument " << k;
        }
    }
}

struct SlopeCase {
    FunctionId id;
    std::vector<double> point;
    std::vector<double> radius;
};

}  // namespace

TEST(Slopes, OperatorsDominateTheirDerivativesOverTheirIntervals) {
    using F = FunctionId;
    const SlopeCase cases[] = {
        {F::Add, {3, 5}, {1, 2}},          {F::Subtract, {3, 5}, {1, 2}},        {F::Negate, {2}, {1}},
        {F::Percent, {2}, {1}},            {F::Abs, {0.5}, {1}},                 {F::Abs, {-2}, {0}},
        {F::Multiply, {3, -5}, {0.5, 0.25}}, {F::Multiply, {0, 0}, {1e-17, 1e-17}}, {F::Multiply, {3, -5}, {0, 0}},
        {F::Divide, {1, 4}, {1, 2}},       {F::Divide, {-3, -0.5}, {0.5, 0.25}}, {F::Divide, {1, 4}, {0, 0}},
        {F::Square, {0}, {0.5}},           {F::Square, {-3}, {0.5}},             {F::Square, {-3}, {0}},
        {F::Cube, {0}, {1e-17}},           {F::Cube, {-2}, {3}},                 {F::Cube, {-2}, {0}},
        {F::Power, {2, 3}, {1, 0}},        {F::Power, {-2, 3}, {0.5, 0}},        {F::Power, {2, -2}, {0.5, 0}},
        {F::Power, {2, 2.5}, {0.5, 0.25}}, {F::Power, {0.7, 0.5}, {0.1, 0.1}},   {F::Power, {0.7, 0.5}, {0, 0}},
        {F::Power, {2, 0}, {1, 0}},        {F::Mod, {7, 3}, {0.5, 0.25}},        {F::Mod, {-7, 3}, {0, 0}},
    };
    for (const SlopeCase& c : cases) {
        SCOPED_TRACE(std::to_string(static_cast<int>(c.id)) + " at " + std::to_string(c.point[0]));
        expectSlopesDominate(c.id, c.point, c.radius);
    }
}

TEST(Slopes, OperatorValues) {
    const auto s = [](FunctionId id, std::vector<Ruler> a, std::vector<Ruler> b) { return slopes(id, a, b); };
    using V = std::vector<Ruler>;
    // x·y: x moves with y at its value, then y moves with x anywhere in its interval.
    EXPECT_EQ(s(FunctionId::Multiply, {Ruler(3), Ruler(-5)}, {Ruler(0.5), Ruler(0.25)}), (V{Ruler(5), Ruler(3.5)}));
    EXPECT_EQ(s(FunctionId::Square, {Ruler(0)}, {Ruler(0.5)}), (V{Ruler(1)}));  // the derivative at 0 is 0, the slope is not
    EXPECT_EQ(s(FunctionId::Cube, {Ruler(2)}, {Ruler(1)}), (V{Ruler(27)}));
    EXPECT_EQ(s(FunctionId::Divide, {Ruler(1), Ruler(4)}, {Ruler(1), Ruler(2)}), (V{Ruler(0.25), Ruler(0.5)}));  // 2 / 2²
    EXPECT_EQ(s(FunctionId::Mod, {Ruler(7), Ruler(3)}, {Ruler(1), Ruler(1)}), (V{Ruler(1), Ruler(4)}));        // trunc(8 / 2)
    EXPECT_EQ(s(FunctionId::Factorial, {Ruler(5)}, {Ruler(0)}), (V{Ruler(0)}));
    EXPECT_TRUE(close(s(FunctionId::Power, {Ruler(2), Ruler(3)}, {Ruler(1), Ruler(0)})[0], Ruler(27)));  // 3 · 3²
    EXPECT_TRUE(close(s(FunctionId::Power, {Ruler(2), Ruler(-1)}, {Ruler(1), Ruler(0)})[0], Ruler(1)));  // 1 · 1⁻²
}

TEST(Slopes, InfiniteWhereAnIntervalReachesAnUnboundedDerivative) {
    const auto s = [](FunctionId id, std::vector<Ruler> a, std::vector<Ruler> b, std::size_t k) {
        return slopes(id, a, b)[k];
    };
    EXPECT_FALSE(isFinite(s(FunctionId::Divide, {Ruler(1), Ruler(0.1)}, {Ruler(0), Ruler(0.1)}, 1)));
    EXPECT_FALSE(isFinite(s(FunctionId::Mod, {Ruler(1), Ruler(0.1)}, {Ruler(0), Ruler(0.2)}, 1)));
    EXPECT_FALSE(isFinite(s(FunctionId::Power, {Ruler(0.5), Ruler(-2)}, {Ruler(0.5), Ruler(0)}, 0)));
    EXPECT_FALSE(isFinite(s(FunctionId::Power, {Ruler(0.5), Ruler(0.5)}, {Ruler(0.6), Ruler(0)}, 0)));
    EXPECT_FALSE(isFinite(s(FunctionId::Power, {Ruler(0.5), Ruler(2.5)}, {Ruler(0.6), Ruler(0)}, 0)));  // x < 0: undefined
    EXPECT_TRUE(isFinite(s(FunctionId::Power, {Ruler(0.5), Ruler(2)}, {Ruler(0.6), Ruler(0)}, 0)));     // x² is smooth at 0
}

TEST(Slopes, FunctionsDominateTheirDerivativesOverTheirIntervals) {
    using F = FunctionId;
    const SlopeCase cases[] = {
        {F::Sqrt, {0.7}, {0.2}},   {F::Cbrt, {-0.7}, {0.2}},  {F::Cbrt, {0.7}, {0.2}},  {F::Exp, {0.7}, {0.5}},
        {F::Exp, {-3}, {1}},       {F::Ln, {0.7}, {0.2}},     {F::Log10, {0.7}, {0.2}}, {F::Sin, {0.7}, {0.3}},
        {F::Sin, {3}, {2}},        {F::Cos, {0.7}, {0.3}},    {F::Tan, {0.7}, {0.3}},   {F::Tan, {-2}, {0.3}},
        {F::Asin, {0.5}, {0.3}},   {F::Acos, {-0.5}, {0.3}},  {F::Atan, {0.7}, {2}},    {F::Sinh, {-0.7}, {0.3}},
        {F::Cosh, {0.7}, {0.3}},   {F::Cosh, {0.1}, {0.3}},   {F::Tanh, {0.7}, {2}},    {F::Asinh, {-0.7}, {0.3}},
        {F::Acosh, {1.7}, {0.3}},  {F::Atanh, {0.3}, {0.3}},  {F::Sqrt, {0.7}, {0}},    {F::Tan, {0.7}, {0}},
        {F::Root, {0.7, 3}, {0.2, 0}}, {F::Root, {-8, 3}, {1, 0}}, {F::Root, {0.7, 3}, {0.2, 0.5}},
        {F::Root, {32, 5}, {0, 0}},    {F::LogBase, {8, 2}, {1, 0.5}}, {F::LogBase, {0.5, 0.3}, {0.1, 0.05}},
        {F::LogBase, {8, 2}, {0, 0}},
    };
    for (const SlopeCase& c : cases) {
        SCOPED_TRACE(std::to_string(static_cast<int>(c.id)) + " at " + std::to_string(c.point[0]));
        expectSlopesDominate(c.id, c.point, c.radius);
    }
}

TEST(Slopes, FunctionsAreInfiniteWhereAnIntervalReachesAnUnboundedDerivative) {
    using F = FunctionId;
    const SlopeCase cases[] = {
        {F::Sqrt, {0.5}, {0.5}},   {F::Ln, {0.1}, {0.2}},     {F::Log10, {0.1}, {0.2}}, {F::Tan, {1.5}, {0.1}},
        {F::Asin, {0.95}, {0.1}},  {F::Acos, {-0.95}, {0.1}}, {F::Atanh, {-0.95}, {0.1}}, {F::Acosh, {1.05}, {0.1}},
        {F::Cbrt, {-0.01}, {0.02}}, {F::Root, {0.001, 3}, {0.01, 0}}, {F::LogBase, {0.1, 2}, {0.2, 0}},
    };
    for (const SlopeCase& c : cases) {
        std::vector<Ruler> a, b;
        for (const double x : c.point) a.push_back(Ruler(x));
        for (const double r : c.radius) b.push_back(Ruler(r));
        EXPECT_FALSE(isFinite(slopes(c.id, a, b)[0])) << static_cast<int>(c.id);
    }
    EXPECT_FALSE(isFinite(slopes(F::LogBase, {Ruler(8), Ruler(1.05)}, {Ruler(0), Ruler(0.1)})[1]));  // base near 1
}

TEST(ExactPower, AResultTooLargeToWriteDownIsAnOverflow) {
    Rational out;
    // 0.7^479001600: some 400 million digits above and below the fraction bar.
    EXPECT_EQ(impl::exactPower(Rational(7, 10), Rational(479001600), out), ErrorCode::Overflow);
    EXPECT_EQ(impl::exactPower(Rational(2), Rational(4000000), out), ErrorCode::Overflow);  // 1 204 120 digits
    EXPECT_EQ(impl::exactPower(Rational(2), Rational(3000000), out), std::nullopt);         // 903 090 digits: allowed
    EXPECT_EQ(out, Rational(Integer(1) << 3000000));
    EXPECT_EQ(impl::exactPower(Rational(1, 2), Rational(-3000000), out), std::nullopt);
    EXPECT_EQ(impl::exactPower(Rational(1), Rational(479001600), out), std::nullopt);       // 1 stays 1
    EXPECT_EQ(out, Rational(1));
}

TEST(Slopes, AnInfiniteBoundGivesInfiniteSlopesAtOnce) {
    // An argument whose error is unbounded (an edge reached upstream) never reaches the ruler's kernels.
    const Ruler inf = std::numeric_limits<Ruler>::infinity();
    EXPECT_FALSE(isFinite(slopes(FunctionId::Power, {Ruler(1e32), Ruler(1)}, {Ruler(0), inf})[1]));
    EXPECT_FALSE(isFinite(slopes(FunctionId::Exp, {Ruler(1)}, {inf})[0]));
    EXPECT_FALSE(isFinite(slopes(FunctionId::Sinh, {Ruler(1)}, {inf})[0]));
    EXPECT_FALSE(isFinite(slopes(FunctionId::Cosh, {Ruler(1)}, {inf})[0]));
    EXPECT_EQ(slopes(FunctionId::Sin, {Ruler(1)}, {inf})[0], Ruler(1));   // |cos| <= 1 everywhere
    EXPECT_EQ(slopes(FunctionId::Atan, {Ruler(1)}, {inf})[0], Ruler(1));  // steepest at 0
}

namespace {

// One input of a function: the arguments (exact small values or a named extreme) and what must come out.
struct Expect {
    FunctionId id;
    std::vector<double> args;      // converted exactly to T
    std::optional<double> value;   // exact expected value, or
    std::optional<ErrorCode> error;  // the error it must give
};

const std::vector<Expect>& classes() {
    using F = FunctionId;
    using E = ErrorCode;
    static const std::vector<Expect> list{
        {F::Sqrt, {0}, 0, {}},       {F::Sqrt, {4}, 2, {}},        {F::Sqrt, {-1}, {}, E::DomainError},
        {F::Cbrt, {0}, 0, {}},       {F::Cbrt, {-8}, -2, {}},      {F::Cbrt, {27}, 3, {}},
        {F::Root, {32, 5}, 2, {}},   {F::Root, {-8, 3}, -2, {}},   {F::Root, {-8, 2}, {}, E::DomainError},
        {F::Root, {2, 0}, {}, E::DomainError}, {F::Root, {0, -1}, {}, E::DivisionByZero}, {F::Root, {0, 2}, 0, {}},
        {F::Exp, {0}, 1, {}},        {F::Exp, {1e7}, {}, E::Overflow}, {F::Exp, {-1e7}, 0, {}},
        {F::Ln, {1}, 0, {}},         {F::Ln, {0}, {}, E::DomainError}, {F::Ln, {-1}, {}, E::DomainError},
        {F::Log10, {1}, 0, {}},      {F::Log10, {0}, {}, E::DomainError},
        {F::LogBase, {1, 2}, 0, {}}, {F::LogBase, {8, 1}, {}, E::DomainError}, {F::LogBase, {8, 0}, {}, E::DomainError},
        {F::LogBase, {8, -2}, {}, E::DomainError}, {F::LogBase, {-8, 2}, {}, E::DomainError},
        {F::Sin, {0}, 0, {}},        {F::Cos, {0}, 1, {}},         {F::Tan, {0}, 0, {}},
        {F::Asin, {0}, 0, {}},       {F::Asin, {1.5}, {}, E::DomainError}, {F::Acos, {1}, 0, {}},
        {F::Acos, {-1.5}, {}, E::DomainError}, {F::Atan, {0}, 0, {}},
        {F::Sinh, {0}, 0, {}},       {F::Cosh, {0}, 1, {}},        {F::Tanh, {0}, 0, {}},
        {F::Tanh, {1e6}, 1, {}},     {F::Tanh, {-1e6}, -1, {}},    {F::Sinh, {1e7}, {}, E::Overflow},
        {F::Asinh, {0}, 0, {}},      {F::Acosh, {1}, 0, {}},       {F::Acosh, {0.5}, {}, E::DomainError},
        {F::Atanh, {0}, 0, {}},      {F::Atanh, {1}, {}, E::DomainError}, {F::Atanh, {-1}, {}, E::DomainError},
        {F::Abs, {-3}, 3, {}},       {F::Abs, {0}, 0, {}},         {F::Abs, {2.5}, 2.5, {}},
        {F::Mod, {7, 3}, 1, {}},     {F::Mod, {-7, 3}, -1, {}},    {F::Mod, {7, -3}, 1, {}},  {F::Mod, {7, 0}, {}, E::DivisionByZero},
        {F::Mod, {0, 3}, 0, {}},     {F::Mod, {5.5, 2}, 1.5, {}},
        {F::Gcd, {12, 18}, 6, {}},   {F::Gcd, {0, 5}, 5, {}},      {F::Gcd, {-12, 18}, 6, {}}, {F::Gcd, {1.5, 3}, {}, E::NotAnInteger},
        {F::Lcm, {4, 6}, 12, {}},    {F::Lcm, {0, 5}, 0, {}},      {F::Lcm, {-4, 6}, 12, {}},
        {F::Ncr, {5, 2}, 10, {}},    {F::Ncr, {5, 0}, 1, {}},      {F::Ncr, {5, 6}, 0, {}},   {F::Ncr, {-5, 2}, {}, E::DomainError},
        {F::Npr, {5, 2}, 20, {}},    {F::Npr, {5, 5}, 120, {}},    {F::Npr, {5, 6}, 0, {}},
        {F::Factorial, {0}, 1, {}},  {F::Factorial, {5}, 120, {}}, {F::Factorial, {-1}, {}, E::DomainError},
        {F::Factorial, {2.5}, {}, E::NotAnInteger},
        {F::Power, {0, 0}, 1, {}},   {F::Power, {0, 3}, 0, {}},    {F::Power, {0, -1}, {}, E::DivisionByZero},
        {F::Power, {-2, 3}, -8, {}}, {F::Power, {-8, 0.5}, {}, E::DomainError}, {F::Power, {2, -2}, 0.25, {}},
        {F::Percent, {50}, 0.5, {}}, {F::Square, {-3}, 9, {}},     {F::Cube, {-2}, -8, {}},   {F::Negate, {0}, 0, {}},
        {F::Divide, {1, 0}, {}, E::DivisionByZero}, {F::Divide, {0, 5}, 0, {}},
        {F::Median, {3}, 3, {}},     {F::Median, {3, 1, 2}, 2, {}}, {F::Median, {4, 1, 3, 2}, 2.5, {}},
    };
    return list;
}

}  // namespace

TYPED_TEST(ApplyTest, EveryExistingFunctionOnItsInputClasses) {
    using T = TypeParam;
    for (const Expect& c : classes()) {
        std::vector<T> args;
        for (const double a : c.args) args.push_back(T(a));
        const Applied<T> r = applyFunction<T>(c.id, args);
        const std::string where = std::string(functionInfo(c.id).name) + " #" + std::to_string(&c - classes().data());
        if (isExact<T> && !functionInfo(c.id).exact) {  // checked first: Exact refuses before any domain check
            ASSERT_TRUE(r.error) << where;
            EXPECT_EQ(*r.error, ErrorCode::NotAvailableInExact) << where;
            continue;
        }
        if (c.error) {
            ASSERT_TRUE(r.error) << where;
            EXPECT_EQ(*r.error, *c.error) << where;
            continue;
        }
        ASSERT_FALSE(r.error) << where;
        EXPECT_EQ(r.value, T(*c.value)) << where;
    }
}

TYPED_TEST(ApplyTest, ExtremesOfTheTypeStayFiniteOrSayOverflow) {
    using T = TypeParam;
    if constexpr (!isExact<T>) {
        const T big = (std::numeric_limits<T>::max)();
        const T tiny = (std::numeric_limits<T>::min)();
        for (FunctionId id : {FunctionId::Sqrt, FunctionId::Cbrt, FunctionId::Ln, FunctionId::Atan, FunctionId::Tanh,
                              FunctionId::Asinh, FunctionId::Abs})
            for (const T& x : {big, tiny, T(-tiny)}) {
                const Applied<T> r = applyFunction<T>(id, {x});
                if (r.error) EXPECT_TRUE(*r.error == ErrorCode::DomainError || *r.error == ErrorCode::Overflow);
                else EXPECT_TRUE(isFinite(r.value)) << static_cast<int>(id);
            }
        EXPECT_EQ(applyFunction<T>(FunctionId::Multiply, {big, T(2)}).error.value_or(ErrorCode::Cancelled), ErrorCode::Overflow);
        EXPECT_EQ(applyFunction<T>(FunctionId::Square, {big}).error.value_or(ErrorCode::Cancelled), ErrorCode::Overflow);
        EXPECT_EQ(applyFunction<T>(FunctionId::Exp, {big}).error.value_or(ErrorCode::Cancelled), ErrorCode::Overflow);
    }
}

TEST(FunctionInfo, OperatorsAreNamedByTheirSigns) {
    EXPECT_EQ(symbolOf(FunctionId::Divide), "÷");
    EXPECT_EQ(symbolOf(FunctionId::Power), "^");
    EXPECT_EQ(symbolOf(FunctionId::Factorial), "!");
    EXPECT_EQ(symbolOf(FunctionId::Sqrt), "sqrt");
}

TEST(FunctionInfo, EveryFunctionTakesItsArgumentCount) {
    using F = FunctionId;
    const std::tuple<F, int, int> arity[] = {
        {F::Literal, 0, 0}, {F::Pi, 0, 0}, {F::E, 0, 0}, {F::Add, 2, 2}, {F::Subtract, 2, 2}, {F::Multiply, 2, 2},
        {F::Divide, 2, 2}, {F::Negate, 1, 1}, {F::Power, 2, 2}, {F::Percent, 1, 1}, {F::Square, 1, 1},
        {F::Cube, 1, 1}, {F::Factorial, 1, 1}, {F::Sqrt, 1, 1}, {F::Cbrt, 1, 1}, {F::Root, 2, 2}, {F::Exp, 1, 1},
        {F::Ln, 1, 1}, {F::Log10, 1, 1}, {F::LogBase, 2, 2}, {F::Sin, 1, 1}, {F::Cos, 1, 1}, {F::Tan, 1, 1},
        {F::Asin, 1, 1}, {F::Acos, 1, 1}, {F::Atan, 1, 1}, {F::Sinh, 1, 1}, {F::Cosh, 1, 1}, {F::Tanh, 1, 1},
        {F::Asinh, 1, 1}, {F::Acosh, 1, 1}, {F::Atanh, 1, 1}, {F::Abs, 1, 1}, {F::Mod, 2, 2}, {F::Gcd, 2, 2},
        {F::Lcm, 2, 2}, {F::Ncr, 2, 2}, {F::Npr, 2, 2}, {F::Median, 1, -1}
    };
    for (const auto& [id, minArgs, maxArgs] : arity) {
        EXPECT_EQ(functionInfo(id).minArgs, minArgs) << static_cast<int>(id);
        EXPECT_EQ(functionInfo(id).maxArgs, maxArgs) << static_cast<int>(id);
    }
}

TYPED_TEST(ApplyTest, LcmWithOne) {
    using T = TypeParam;
    EXPECT_EQ(applyFunction<T>(FunctionId::Lcm, {T(1), T(5)}).value, T(5));
    EXPECT_EQ(applyFunction<T>(FunctionId::Lcm, {T(5), T(1)}).value, T(5));
}

TEST(LocalError, ExactResultsHaveNone) {
    const auto local = [](FunctionId id, const std::vector<double>& a) {
        return localError<double>(id, a, applyFunction<double>(id, a));
    };
    EXPECT_EQ(local(FunctionId::Cube, {2}), 0);
    EXPECT_EQ(local(FunctionId::Lcm, {4, 6}), 0);
    EXPECT_EQ(local(FunctionId::Root, {1, 3}), 0);
    EXPECT_EQ(local(FunctionId::Root, {8, 3}), 0);
    EXPECT_EQ(local(FunctionId::Root, {0.125, -3}), 0);
    EXPECT_GT(local(FunctionId::Root, {2, 3}), 0);  // irrational
}

TEST(Partials, NegationFlipsTheSign) {
    EXPECT_EQ(partials<Ruler>(FunctionId::Negate, {Ruler(3)}, Ruler(-3)), (std::vector<Ruler>{Ruler(-1)}));
}

TEST(Slopes, MoreBoundaries) {
    // x^1 is linear: slope 1 even where the interval reaches 0.
    EXPECT_EQ(slopes(FunctionId::Power, {Ruler(0), Ruler(1)}, {Ruler(0.5), Ruler(0)})[0], Ruler(1));
    // An even count: the medians of the ends are (1 + 1.4)/2 and (1 + 2)/2, and 10 ± 8.6 reaches that range.
    EXPECT_EQ(slopes(FunctionId::Median, {Ruler(0), Ruler(1), Ruler(2), Ruler(10)},
                     {Ruler(0), Ruler(0), Ruler(0), Ruler(8.6)}),
              (std::vector<Ruler>{Ruler(0), Ruler(0.5), Ruler(0.5), Ruler(1)}));
    using F = FunctionId;
    const SlopeCase cases[] = {
        {F::Power, {0.5, -2}, {0.1, 0.5}}, {F::Root, {32, 5}, {1, 0.5}},  // the largest power at the lower corner
        {F::Root, {0.5, -3}, {0.1, 0.5}},  // and at the upper corner of a negative order
        {F::Cbrt, {0.7}, {0}},   {F::Exp, {0.7}, {0}},   {F::Ln, {0.7}, {0}},    {F::Log10, {0.7}, {0}},
        {F::Sin, {0.7}, {0}},    {F::Cos, {0.7}, {0}},   {F::Asin, {0.5}, {0}},  {F::Acos, {-0.5}, {0}},
        {F::Atan, {0.7}, {0}},   {F::Sinh, {-0.7}, {0}}, {F::Cosh, {-0.7}, {0}}, {F::Tanh, {0.7}, {0}},
        {F::Asinh, {-0.7}, {0}}, {F::Acosh, {1.7}, {0}}, {F::Atanh, {0.3}, {0}},
    };
    for (const SlopeCase& c : cases) {
        SCOPED_TRACE(std::to_string(static_cast<int>(c.id)) + " at " + std::to_string(c.point[0]));
        expectSlopesDominate(c.id, c.point, c.radius);
    }
}
