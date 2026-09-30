#include "test_support.hpp"

using namespace calculate_core::detail;

using Emulated32 = mp::number<mp::cpp_bin_float<24, mp::digit_base_2, void, std::int16_t, -126, 127>, mp::et_off>;
using Emulated64 = mp::number<mp::cpp_bin_float<53, mp::digit_base_2, void, std::int16_t, -1022, 1023>, mp::et_off>;

// Rump's polynomial at (77617, 33096), in a fixed evaluation order.
template <class T>
T rump() {
    const T a = 77617, b = 33096;
    const T b2 = b * b, b4 = b2 * b2, b6 = b4 * b2, b8 = b4 * b4, a2 = a * a;
    return T(333.75) * b6 + a2 * (T(11) * a2 * b2 - b6 - T(121) * b4 - T(2)) + T(5.5) * b8
           + a / (T(2) * b);
}

TEST(IeeeConformance, FloatMatchesSoftwareBinary32) {
    EXPECT_EQ(toRational(rump<float>()), toRational(rump<Emulated32>()));
}

TEST(IeeeConformance, DoubleMatchesSoftwareBinary64) {
    EXPECT_EQ(toRational(rump<double>()), toRational(rump<Emulated64>()));
}
