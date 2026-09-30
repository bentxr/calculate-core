#include "doubleword.hpp"
#include "test_support.hpp"

using namespace calculate_core::detail;

namespace {

template <class T>
DoubleWord<T> randomWord(std::mt19937_64& rng) {
    using std::ldexp;
    const T hi = test::randomFinite<T>(rng, 20);
    const T lo = ldexp(test::randomFinite<T>(rng, 0), -precisionBits<T>() - 2) * hi;  // |lo| < ulp(hi)/2
    const TwoPart<T> s = fastTwoSum(hi, lo);
    return {s.value, s.error};
}

template <class T>
Rational exact(const DoubleWord<T>& x) { return toRational(x.hi) + toRational(x.lo); }

// |computed - exact| <= 16 u^2 |exact|
template <class T>
bool accurate(const DoubleWord<T>& computed, const Rational& exactValue) {
    const Rational u = toRational(unitRoundoff<T>());
    return abs(exact(computed) - exactValue) <= 16 * u * u * abs(exactValue);
}

}  // namespace

template <class T>
class DoubleWordTest : public ::testing::Test {};
TYPED_TEST_SUITE(DoubleWordTest, test::FloatingTypes, test::TypeNames);

TYPED_TEST(DoubleWordTest, ArithmeticHasAboutTwiceThePrecision) {
    using T = TypeParam;
    std::mt19937_64 rng(11);
    for (int i = 0; i < 200; ++i) {
        const DoubleWord<T> x = randomWord<T>(rng), y = randomWord<T>(rng);
        const T z = test::randomFinite<T>(rng, 20);
        EXPECT_TRUE(accurate(x + y, exact(x) + exact(y)));
        EXPECT_TRUE(accurate(x - y, exact(x) - exact(y)));
        EXPECT_TRUE(accurate(x + z, exact(x) + toRational(z)));
        EXPECT_TRUE(accurate(x * z, exact(x) * toRational(z)));
        EXPECT_TRUE(accurate(x * y, exact(x) * exact(y)));
        EXPECT_TRUE(accurate(x / y, exact(x) / exact(y)));
    }
}

TYPED_TEST(DoubleWordTest, SquareRoot) {
    using T = TypeParam;
    std::mt19937_64 rng(12);
    for (int i = 0; i < 100; ++i) {
        DoubleWord<T> x = randomWord<T>(rng);
        if (x.hi < 0) x = -x;
        const DoubleWord<T> s = sqrt(x);
        EXPECT_TRUE(accurate(s * s, exact(x)));
    }
    EXPECT_EQ(sqrt(dw(T(0))).hi, T(0));
}

TYPED_TEST(DoubleWordTest, ScalingIsExact) {
    using T = TypeParam;
    using std::ldexp;
    const DoubleWord<T> x{T(3), T(0.5) * ldexp(T(1), -precisionBits<T>())};
    EXPECT_EQ(exact(scale(x, 5)), exact(x) * 32);
    EXPECT_EQ(exact(scale(x, -3)), exact(x) / 8);
}
