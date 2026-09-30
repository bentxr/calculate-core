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

TEST(Cli, TheHeadlineExample) {
    const Outcome r = invoke({"0.1 + 0.2"});
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.out,
              "0.1 + 0.2\n"
              "= 0.300000000000000|0444089209850062616169452667236328125\n"
              "  ± 4.4e-17  input 1.7e-17 · rounding 2.8e-17 · library 0\n"
              "  measured 4.4e-17 · κ 1e+0 · 15 trusted digits\n");
    EXPECT_EQ(r.err, "");
}

TEST(Cli, ColourOnlyOnTerminalsUnlessAsked) {
    EXPECT_EQ(invoke({"0.1 + 0.2"}, "", true).out.find("\x1b[2m") != std::string::npos, true);
    EXPECT_EQ(invoke({"--color", "never", "0.1 + 0.2"}, "", true).out.find("\x1b"), std::string::npos);
    EXPECT_NE(invoke({"--color", "always", "0.1 + 0.2"}).out.find("\x1b[2m"), std::string::npos);
}

TEST(Cli, ExactResults) {
    const Outcome r = invoke({"--type", "exact", "1/3"});
    EXPECT_EQ(r.out, "1/3\n= 1/3 = 0.(3)\n  exact, no rounding error · κ 2e+0\n");
}

TEST(Cli, ErrorsGoToStandardErrorWithACaret) {
    const Outcome r = invoke({"1 + 1/0"});
    EXPECT_EQ(r.code, 1);
    EXPECT_EQ(r.out, "");
    EXPECT_EQ(r.err, "1 + 1/0\n    ^^^ Division by zero\n");
    const Outcome wide = invoke({"2×π + 1/0"});  // columns count code points, not bytes
    EXPECT_EQ(wide.err, "2×π + 1/0\n      ^^^ Division by zero\n");
}

TEST(Cli, OneSessionAcrossExpressions) {
    const Outcome r = invoke({"--color", "never", "2", "Ans*3", "M+", "Ans+1", "M-", "MC"});
    EXPECT_EQ(r.code, 0);
    EXPECT_NE(r.out.find("Ans*3\n= 6\n"), std::string::npos);
    EXPECT_NE(r.out.find("M = (2)*3\n"), std::string::npos);
    EXPECT_NE(r.out.find("Ans+1\n= 7\n"), std::string::npos);
    EXPECT_NE(r.out.find("M = (2)*3-(((2)*3)+1)\n"), std::string::npos);
    EXPECT_NE(r.out.find("M cleared\n"), std::string::npos);
    EXPECT_EQ(invoke({"M+"}).code, 1);
}

TEST(Cli, ReadsStandardInputWhenGivenNoExpressions) {
    const Outcome r = invoke({"--type", "exact"}, "# a comment\n1/2\n\nAns + 1/3\r\n");
    EXPECT_EQ(r.code, 0);
    EXPECT_NE(r.out.find("= 5/6 = 0.8(3)"), std::string::npos);
}

TEST(Cli, EscapesJsonStrings) {
    EXPECT_EQ(calc::jsonString("a\"b\\c\n\x01"), "\"a\\\"b\\\\c\\n\\u0001\"");
}

TEST(Cli, JsonLines) {
    const Outcome r = invoke({"--json", "1/0", "2+2"});
    EXPECT_EQ(r.code, 1);
    EXPECT_EQ(r.out.substr(0, r.out.find('\n') + 1),
              "{\"expression\":\"1/0\",\"type\":\"double\",\"error\":{\"code\":\"DivisionByZero\","
              "\"message\":\"Division by zero\",\"begin\":0,\"end\":3}}\n");
    EXPECT_NE(r.out.find("\"value\":{\"negative\":false,\"digits\":\"4\",\"exponent10\":0}"), std::string::npos);
}
