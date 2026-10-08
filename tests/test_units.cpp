#include "units.hpp"

#include <gtest/gtest.h>

using namespace calculate_core::detail;

namespace {

Dimension of(std::initializer_list<int> exponents) {
    Dimension d;
    int i = 0;
    for (const int e : exponents) d.exponents[static_cast<std::size_t>(i++)] = e;
    return d;
}

}  // namespace

TEST(Units, DimensionsMultiplyAndDivide) {
    const Dimension speed = of({1, 0, -1});
    const Dimension time = of({0, 0, 1});
    EXPECT_EQ(speed * time, of({1}));
    EXPECT_EQ(speed / time, of({1, 0, -2}));
    EXPECT_EQ(power(speed, Rational(2)), of({2, 0, -2}));
    EXPECT_EQ(power(of({2}), Rational(1, 2)), of({1}));
    EXPECT_TRUE(Dimension{}.none());
    EXPECT_FALSE(speed.none());
}

TEST(Units, NamesOfUnits) {
    EXPECT_EQ(unitName(of({1, 0, -1})), "m·s⁻¹");
    EXPECT_EQ(unitName(of({3, -1, -2})), "m³·kg⁻¹·s⁻²");
    EXPECT_EQ(unitName(of({0, 1})), "kg");
    EXPECT_EQ(unitName(of({2, 1, -2})), "J");                    // named derived units
    EXPECT_EQ(unitName(of({2, 1, -1})), "J·s");                  // a named unit times one base unit
    EXPECT_EQ(unitName(of({2, 1, -2, 0, -1})), "J·K⁻¹");
    EXPECT_EQ(unitName(of({0, 0, 1, 1})), "C");
    EXPECT_EQ(unitName(of({2, 1, -3, -1})), "V");
    EXPECT_EQ(unitName(of({3, 1, -3})), "W·m");                  // not an exact name: the first named unit that fits
    EXPECT_EQ(unitName(of({3, 1, -2})), "J·m");
    EXPECT_EQ(unitName(of({2, 0, -2})), "m²·s⁻²");               // two base units: written as they are
    EXPECT_EQ(unitName(Dimension{}), "");
    Dimension half;
    half.exponents[0] = Rational(1, 2);
    EXPECT_EQ(unitName(half), "m^(1/2)");                       // fractional exponents are written out
}

// Mutation survivors (Plan 3's checkpoint): the named units past the first few.
TEST(Units, MoreNamedUnits) {
    EXPECT_EQ(unitName(of({-1, 1, -2})), "Pa");
    EXPECT_EQ(unitName(of({-2, -1, 4, 2})), "F");
    EXPECT_EQ(unitName(of({2, 1, -3, -2})), "\xCE\xA9");
}
