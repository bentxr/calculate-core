#include "accuracy.hpp"

using namespace calculate_core;
using namespace calculate_core::detail;
using test::logUniform;
using test::randomSign;
using test::uniform;
using std::ldexp;

template <class T>
class KernelTest : public ::testing::Test {};
TYPED_TEST_SUITE(KernelTest, test::FloatingTypes, test::TypeNames);

// Largest |x| with a finite exp(x), a little inside the range.
template <class T>
double expRange() { return std::min(700.0, 0.69 * maxExponent<T>()); }

TYPED_TEST(KernelTest, Exp) {
    using T = TypeParam;
    test::expectWithinClaim<T>(FunctionId::Exp, [](auto& rng) { return std::pair<T, T>{uniform<T>(rng, -expRange<T>(), expRange<T>()), T(0)}; });
    test::expectWithinClaim<T>(FunctionId::Exp, [](auto& rng) { return std::pair<T, T>{randomSign(rng, logUniform<T>(rng, -60, -1)), T(0)}; });
}

TYPED_TEST(KernelTest, ExpEdges) {
    using T = TypeParam;
    EXPECT_EQ(applyFunction<T>(FunctionId::Exp, {T(0)}).value, T(1));
    const Applied<T> huge = applyFunction<T>(FunctionId::Exp, {ldexp(T(1), maxExponent<T>() / 2)});
    ASSERT_TRUE(huge.error);
    EXPECT_EQ(*huge.error, ErrorCode::Overflow);
    const Applied<T> tiny = applyFunction<T>(FunctionId::Exp, {-ldexp(T(1), maxExponent<T>() / 2)});
    EXPECT_FALSE(tiny.error);
    EXPECT_EQ(tiny.value, T(0));
}

TYPED_TEST(KernelTest, Logarithms) {
    using T = TypeParam;
    test::expectWithinClaim<T>(FunctionId::Ln, [](auto& rng) { return std::pair<T, T>{logUniform<T>(rng, -200, 200), T(0)}; });
    test::expectWithinClaim<T>(FunctionId::Ln, [](auto& rng) { return std::pair<T, T>{T(T(1) + randomSign(rng, logUniform<T>(rng, -40, -2))), T(0)}; });
    test::expectWithinClaim<T>(FunctionId::Log10, [](auto& rng) { return std::pair<T, T>{logUniform<T>(rng, -100, 100), T(0)}; });
    test::expectWithinClaim<T>(FunctionId::LogBase, [](auto& rng) {
        const T x = logUniform<T>(rng, -100, 100);
        return std::pair<T, T>{x, uniform<T>(rng, 1.5, 20)};
    });
}

TYPED_TEST(KernelTest, LogarithmEdges) {
    using T = TypeParam;
    EXPECT_EQ(applyFunction<T>(FunctionId::Ln, {T(1)}).value, T(0));
    EXPECT_EQ(applyFunction<T>(FunctionId::Log10, {T(1000)}).value, T(3));
    EXPECT_EQ(applyFunction<T>(FunctionId::LogBase, {T(8), T(2)}).value, T(3));
    for (const T& bad : {T(0), T(-1)}) {
        const Applied<T> r = applyFunction<T>(FunctionId::Ln, {bad});
        ASSERT_TRUE(r.error);
        EXPECT_EQ(*r.error, ErrorCode::DomainError);
    }
    const Applied<T> baseOne = applyFunction<T>(FunctionId::LogBase, {T(5), T(1)});
    ASSERT_TRUE(baseOne.error);
    EXPECT_EQ(*baseOne.error, ErrorCode::DomainError);
}

TEST(Reduction, HugeDoubleArgument) {
    // 1e22 = n pi/2 + r: check r against the oracle's pi at 2017 bits.
    const auto reduced = reduceHalfPi(1e22);
    ASSERT_TRUE(reduced);
    using O = test::Oracle;
    const O halfPi = acos(O(-1)) / 2;
    const O x = exactCast<O>(1e22);
    const O n = round(x / halfPi);
    const O r = x - n * halfPi;
    EXPECT_EQ(reduced->quadrant, static_cast<int>(fmod(n, O(4)).convert_to<long long>()));
    const O ours = exactCast<O>(reduced->r.hi) + exactCast<O>(reduced->r.lo);
    EXPECT_LE(abs(ours - r), abs(r) * ldexp(O(1), -100));
}

TYPED_TEST(KernelTest, Trigonometry) {
    using T = TypeParam;
    test::expectWithinClaim<T>(FunctionId::Sin, [](auto& rng) { return std::pair<T, T>{randomSign(rng, logUniform<T>(rng, -20, 100)), T(0)}; });
    test::expectWithinClaim<T>(FunctionId::Cos, [](auto& rng) { return std::pair<T, T>{randomSign(rng, logUniform<T>(rng, -20, 100)), T(0)}; });
    test::expectWithinClaim<T>(FunctionId::Tan, [](auto& rng) { return std::pair<T, T>{randomSign(rng, logUniform<T>(rng, -20, 60)), T(0)}; });
    test::expectWithinClaim<T>(FunctionId::Sin, [](auto& rng) { return std::pair<T, T>{logUniform<T>(rng, 100, 1000), T(0)}; });
}

TYPED_TEST(KernelTest, ArgumentReductionLimit) {
    using T = TypeParam;
    const T limit = ldexp(T(1), std::min(1024, maxExponent<T>()));
    const Applied<T> r = applyFunction<T>(FunctionId::Sin, {limit});
    if (maxExponent<T>() >= 1024) {
        ASSERT_TRUE(r.error);
        EXPECT_EQ(*r.error, ErrorCode::ArgumentTooLarge);
    } else {
        EXPECT_FALSE(r.error);  // float and double cannot reach 2^1024
    }
}

TYPED_TEST(KernelTest, InverseTrigonometry) {
    using T = TypeParam;
    test::expectWithinClaim<T>(FunctionId::Asin, [](auto& rng) { return std::pair<T, T>{uniform<T>(rng, -1, 1), T(0)}; });
    test::expectWithinClaim<T>(FunctionId::Acos, [](auto& rng) { return std::pair<T, T>{uniform<T>(rng, -1, 1), T(0)}; });
    test::expectWithinClaim<T>(FunctionId::Atan, [](auto& rng) { return std::pair<T, T>{randomSign(rng, logUniform<T>(rng, -30, 30)), T(0)}; });
    EXPECT_EQ(applyFunction<T>(FunctionId::Asin, {T(1)}).value, constantValue<T>(ConstantId::Pi) / 2);
    EXPECT_EQ(applyFunction<T>(FunctionId::Acos, {T(-1)}).value, constantValue<T>(ConstantId::Pi));
    const Applied<T> outside = applyFunction<T>(FunctionId::Asin, {T(1.5)});
    ASSERT_TRUE(outside.error);
    EXPECT_EQ(*outside.error, ErrorCode::DomainError);
}

TYPED_TEST(KernelTest, Hyperbolic) {
    using T = TypeParam;
    test::expectWithinClaim<T>(FunctionId::Sinh, [](auto& rng) { return std::pair<T, T>{uniform<T>(rng, -expRange<T>(), expRange<T>()), T(0)}; });
    test::expectWithinClaim<T>(FunctionId::Sinh, [](auto& rng) { return std::pair<T, T>{randomSign(rng, logUniform<T>(rng, -30, 0)), T(0)}; });
    test::expectWithinClaim<T>(FunctionId::Cosh, [](auto& rng) { return std::pair<T, T>{uniform<T>(rng, -expRange<T>(), expRange<T>()), T(0)}; });
    test::expectWithinClaim<T>(FunctionId::Tanh, [](auto& rng) { return std::pair<T, T>{uniform<T>(rng, -20, 20), T(0)}; });
    test::expectWithinClaim<T>(FunctionId::Tanh, [](auto& rng) { return std::pair<T, T>{randomSign(rng, logUniform<T>(rng, -30, -1)), T(0)}; });
}

TYPED_TEST(KernelTest, InverseHyperbolic) {
    using T = TypeParam;
    test::expectWithinClaim<T>(FunctionId::Asinh, [](auto& rng) { return std::pair<T, T>{randomSign(rng, logUniform<T>(rng, -30, 60)), T(0)}; });
    test::expectWithinClaim<T>(FunctionId::Acosh, [](auto& rng) { return std::pair<T, T>{T(T(1) + logUniform<T>(rng, -30, 60)), T(0)}; });
    test::expectWithinClaim<T>(FunctionId::Atanh, [](auto& rng) { return std::pair<T, T>{uniform<T>(rng, -0.999, 0.999), T(0)}; });
    for (const auto& [id, x] : {std::pair<FunctionId, T>{FunctionId::Acosh, T(0.5)}, {FunctionId::Atanh, T(1)}, {FunctionId::Atanh, T(-2)}}) {
        const Applied<T> r = applyFunction<T>(id, {x});
        ASSERT_TRUE(r.error);
        EXPECT_EQ(*r.error, ErrorCode::DomainError);
    }
}

TYPED_TEST(KernelTest, PowersAndRoots) {
    using T = TypeParam;
    const double yMax = std::min(40.0, maxExponent<T>() / 20.0);
    test::expectWithinClaim<T>(FunctionId::Power, [yMax](auto& rng) {
        const T x = logUniform<T>(rng, -10, 10);
        return std::pair<T, T>{x, uniform<T>(rng, -yMax, yMax)};
    });
    test::expectWithinClaim<T>(FunctionId::Cbrt, [](auto& rng) { return std::pair<T, T>{randomSign(rng, logUniform<T>(rng, -100, 100)), T(0)}; });
    test::expectWithinClaim<T>(FunctionId::Root, [](auto& rng) {
        const T x = logUniform<T>(rng, -100, 100);
        return std::pair<T, T>{x, T(2 + static_cast<int>(rng() % 6))};
    });
}

TYPED_TEST(KernelTest, PowerFollowsIeeeRules) {
    using T = TypeParam;
    EXPECT_EQ(applyFunction<T>(FunctionId::Power, {T(0), T(0)}).value, T(1));
    EXPECT_EQ(applyFunction<T>(FunctionId::Power, {T(-2), T(3)}).value, T(-8));
    EXPECT_EQ(applyFunction<T>(FunctionId::Power, {T(-2), T(2)}).value, T(4));
    EXPECT_EQ(applyFunction<T>(FunctionId::Power, {T(2), T(10)}).value, T(1024));
    EXPECT_EQ(applyFunction<T>(FunctionId::Power, {T(2), T(-2)}).value, T(0.25));
    EXPECT_EQ(applyFunction<T>(FunctionId::Root, {T(-8), T(3)}).value, T(-2));
    EXPECT_EQ(applyFunction<T>(FunctionId::Cbrt, {T(-27)}).value, T(-3));
    EXPECT_EQ(applyFunction<T>(FunctionId::Sqrt, {T(2.25)}).value, T(1.5));
    const std::pair<FunctionId, std::vector<T>> domainErrors[] = {
        {FunctionId::Power, {T(-8), T(1) / T(3)}}, {FunctionId::Root, {T(-8), T(2)}},
        {FunctionId::Sqrt, {T(-1)}}, {FunctionId::Root, {T(8), T(0)}}};
    for (const auto& [id, args] : domainErrors) {
        const Applied<T> r = applyFunction<T>(id, args);
        ASSERT_TRUE(r.error) << static_cast<int>(id);
        EXPECT_EQ(*r.error, ErrorCode::DomainError);
    }
    const Applied<T> zeroToNegative = applyFunction<T>(FunctionId::Power, {T(0), T(-1)});
    ASSERT_TRUE(zeroToNegative.error);
    EXPECT_EQ(*zeroToNegative.error, ErrorCode::DivisionByZero);
}

TEST(KernelAccuracy, TheRulerItselfIsAccurate) {
    using T = Ruler;
    std::mt19937_64 rng(5);
    for (FunctionId id : {FunctionId::Exp, FunctionId::Ln, FunctionId::Sin, FunctionId::Atan, FunctionId::Power}) {
        const T x = logUniform<T>(rng, -3, 3), y = uniform<T>(rng, -3, 3);
        std::vector<T> args{x};
        if (functionInfo(id).minArgs == 2) args.push_back(y);
        const Applied<T> r = applyFunction<T>(id, args);
        ASSERT_FALSE(r.error);
        EXPECT_LE(test::errorInU(r.value, test::oracle(id, exactCast<test::Oracle>(x), exactCast<test::Oracle>(y))),
                  claimedFactor(id));
    }
}

TYPED_TEST(KernelTest, AtanOfTheLargestValues) {
    using T = TypeParam;
    // Near the maximum, 1/a as a double word would split a and overflow; atan(a) is pi/2 - 1/a there.
    const T big = (std::numeric_limits<T>::max)();
    for (const T& x : {big, T(-big), ldexp(T(1), maxExponent<T>() / 2)}) {
        const Applied<T> r = applyFunction<T>(FunctionId::Atan, {x});
        ASSERT_FALSE(r.error);
        EXPECT_EQ(abs(r.value), toValue(impl::halfPi<T>()));  // 1/a is far below half an ulp of pi/2
        EXPECT_EQ(r.value < 0, x < 0);
    }
}

TYPED_TEST(KernelTest, AtanSeriesEndsEvenOnANaN) {
    using T = TypeParam;
    EXPECT_FALSE(isFinite(atanWord(dw(std::numeric_limits<T>::quiet_NaN())).hi));
}
