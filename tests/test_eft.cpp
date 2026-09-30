#include "eft.hpp"
#include "test_support.hpp"

using namespace calculate_core::detail;

template <class T>
class EftTest : public ::testing::Test {};
TYPED_TEST_SUITE(EftTest, test::FloatingTypes, test::TypeNames);

TYPED_TEST(EftTest, TwoSumIsExact) {
    using T = TypeParam;
    std::mt19937_64 rng(1);
    for (int i = 0; i < 500; ++i) {
        const T a = test::randomFinite<T>(rng, 40), b = test::randomFinite<T>(rng, 40);
        const TwoPart<T> s = twoSum(a, b);
        EXPECT_EQ(s.value, a + b);
        EXPECT_EQ(toRational(s.value) + toRational(s.error), toRational(a) + toRational(b));
    }
}

TYPED_TEST(EftTest, FastTwoSumIsExactWhenTheFirstIsLarger) {
    using T = TypeParam;
    using std::abs;
    std::mt19937_64 rng(2);
    for (int i = 0; i < 500; ++i) {
        T a = test::randomFinite<T>(rng, 40), b = test::randomFinite<T>(rng, 40);
        if (abs(a) < abs(b)) std::swap(a, b);
        const TwoPart<T> s = fastTwoSum(a, b);
        EXPECT_EQ(toRational(s.value) + toRational(s.error), toRational(a) + toRational(b));
    }
}

TYPED_TEST(EftTest, TwoProductIsExact) {
    using T = TypeParam;
    std::mt19937_64 rng(3);
    for (int i = 0; i < 500; ++i) {
        const T a = test::randomFinite<T>(rng, 40), b = test::randomFinite<T>(rng, 40);
        const TwoPart<T> p = twoProduct(a, b);
        EXPECT_EQ(p.value, a * b);
        EXPECT_EQ(toRational(p.value) + toRational(p.error), toRational(a) * toRational(b));
    }
}

TYPED_TEST(EftTest, TwoProductResidualOfAKnownSquare) {
    // a = 1 + 2^-s with s = ceil(p/2): a^2 = 1 + 2^(1-s) + 2^-2s, and the last term is the error.
    using T = TypeParam;
    using std::ldexp;
    const int s = (precisionBits<T>() + 1) / 2;
    const T a = T(1) + ldexp(T(1), -s);
    const TwoPart<T> p = twoProduct(a, a);
    EXPECT_EQ(p.value, T(1) + ldexp(T(1), 1 - s));
    EXPECT_EQ(p.error, ldexp(T(1), -2 * s));
}
