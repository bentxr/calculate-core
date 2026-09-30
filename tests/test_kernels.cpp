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
