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

TEST(FunctionInfo, DiscreteAndExactFlags) {
    for (FunctionId id : {FunctionId::Factorial, FunctionId::Gcd, FunctionId::Lcm, FunctionId::Ncr, FunctionId::Npr})
        EXPECT_TRUE(functionInfo(id).discrete);
    EXPECT_FALSE(functionInfo(FunctionId::Mod).discrete);
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
