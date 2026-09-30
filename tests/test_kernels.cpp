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
