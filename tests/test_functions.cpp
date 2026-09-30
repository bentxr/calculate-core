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
