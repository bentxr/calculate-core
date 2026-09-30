#include <calculate-core/calculate-core.hpp>

#include <gtest/gtest.h>

using namespace calculate_core;

TEST(PublicTypes, DefaultOptions) {
    const Options options;
    EXPECT_EQ(options.type, NumberType::Double);
    EXPECT_EQ(options.angle, AngleUnit::Radians);
    EXPECT_FALSE(options.allowUncertainDiscreteArguments);
    EXPECT_EQ(options.cancel, nullptr);
}

TEST(PublicTypes, ErrorsCarryASpan) {
    const Error error{ErrorCode::DivisionByZero, "Division by zero", 2, 3};
    EXPECT_EQ(error.end - error.begin, 1u);
}
