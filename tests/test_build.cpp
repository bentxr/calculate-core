#include <boost/multiprecision/cpp_int.hpp>
#include <gtest/gtest.h>

TEST(Build, ExceptionsAreDisabled) {
#ifdef __cpp_exceptions
    FAIL() << "the build must use -fno-exceptions";
#endif
}

TEST(Build, BoostMultiprecisionIsAvailable) {
    using Rational = boost::multiprecision::number<boost::multiprecision::cpp_rational_backend,
                                                   boost::multiprecision::et_off>;
    EXPECT_EQ(Rational(1, 3) * 3, 1);
}

TEST(Build, FloatingPointContractionIsOff) {
    // (1 + 2^-27)^2 - (1 + 2^-26) is 0 when a*b is rounded before the addition,
    // and 2^-54 if the compiler fused it into an fma.
    volatile double a = 1.0 + 0x1p-27;
    double b = a;
    double c = -(1.0 + 0x1p-26);
    EXPECT_EQ(a * b + c, 0.0);
}
