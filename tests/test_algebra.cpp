#include <gtest/gtest.h>

#include <calculate-core/algebra.hpp>

// No public functions are exported from the library yet, so this placeholder
// just proves the harness compiles, links against calculate-core, and runs.
//
// When you add a function -- e.g.
//   CALCULATE_CORE_API double determinant2x2(double a, double b,
//                                             double c, double d);
// replace this with real cases. Use EXPECT_NEAR for floating point, never
// EXPECT_EQ on doubles:
//
//   TEST(Algebra, Determinant2x2) {
//       EXPECT_NEAR(calculate_core::determinant2x2(1, 2, 3, 4), -2.0, 1e-9);
//   }
TEST(Sanity, HarnessWorks) {
    EXPECT_EQ(2 + 2, 4);
}
