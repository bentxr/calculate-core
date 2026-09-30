#include "cli.hpp"

#include <gtest/gtest.h>

#include <sstream>

using namespace calculate_core;

namespace {

struct Outcome {
    int code;
    std::string out;
    std::string err;
};

Outcome invoke(std::vector<std::string> args, const std::string& input = "", bool terminal = false) {
    std::istringstream in(input);
    std::ostringstream out, err;
    const int code = calc::run(args, in, out, err, terminal);
    return {code, out.str(), err.str()};
}

}  // namespace

TEST(Cli, UsageErrors) {
    EXPECT_EQ(invoke({"--help"}).code, 0);
    EXPECT_NE(invoke({"--help"}).out.find("Usage: calc"), std::string::npos);
    EXPECT_EQ(invoke({"--version"}).out, "calc 0.1.0\n");
    EXPECT_EQ(invoke({"--bogus"}).code, 2);
    EXPECT_EQ(invoke({"--type"}).code, 2);
    EXPECT_EQ(invoke({"--type", "quad"}).code, 2);
    EXPECT_EQ(invoke({"--angle", "turns"}).code, 2);
    EXPECT_EQ(invoke({"--color", "sometimes"}).code, 2);
}

TEST(Cli, FormatsEveryDigitWithTheTrustedOnesMarked) {
    const Digits tenths{false, "3000000000000000444089209850062616169452667236328125", -1};
    EXPECT_EQ(calc::formatValue(tenths, 15, false), "0.300000000000000|0444089209850062616169452667236328125");
    EXPECT_EQ(calc::formatValue(tenths, 52, false), "0.3000000000000000444089209850062616169452667236328125");
    EXPECT_EQ(calc::formatValue(tenths, 15, true),
              "0.300000000000000|\x1b[2m0444089209850062616169452667236328125\x1b[0m");
    EXPECT_EQ(calc::formatValue({false, "1", 20}, 1, false), "100000000000000000000");
    EXPECT_EQ(calc::formatValue({false, "1", 21}, 1, false), "1e+21");
    EXPECT_EQ(calc::formatValue({true, "25", 0}, 1, false), "-2.|5");
    EXPECT_EQ(calc::formatValue({false, "123", -10}, 1, false), "1.|23e-10");
    EXPECT_EQ(calc::formatValue({false, "5", -3}, 1, false), "0.005");
    EXPECT_EQ(calc::formatValue({false, "0", 0}, 0, false), "|0");
    EXPECT_EQ(calc::formatValue({false, "0", 0}, 1, false), "0");
}

TEST(Cli, FormatsFractions) {
    EXPECT_EQ(calc::formatFraction({false, "1", "3", true, "0", "", "3"}), "1/3 = 0.(3)");
    EXPECT_EQ(calc::formatFraction({false, "3", "8", true, "0", "375", ""}), "3/8 = 0.375");
    EXPECT_EQ(calc::formatFraction({true, "54767", "66192", false, "", "", ""}), "-54767/66192");
    EXPECT_EQ(calc::formatFraction({false, "10", "1", true, "10", "", ""}), "10");
    EXPECT_EQ(calc::formatFraction({true, "1", "6", true, "0", "1", "6"}), "-1/6 = -0.1(6)");
}
