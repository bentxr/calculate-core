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

TEST(Cli, ListsTheTypesOfThisBuild) {
    const Outcome r = invoke({"--list-types"});
    EXPECT_EQ(r.code, 0);
    EXPECT_NE(r.out.find("double       Double     64-bit, 53-bit significand, ~16 digits\n"), std::string::npos);
    EXPECT_NE(r.out.find("exact        Exact      exact rationals, no rounding\n"), std::string::npos);
}

TEST(Cli, AJumpWithinTheErrorIsAnErrorCode) {
    const Outcome r = invoke({"--json", "mod(0.7+0.1, 0.8)"});
    EXPECT_EQ(r.code, 1);
    EXPECT_NE(r.out.find("\"code\":\"ArgumentNearJump\""), std::string::npos);
    EXPECT_EQ(invoke({"--allow-uncertain", "mod(0.7+0.1, 0.8)"}).code, 0);
}

TEST(Cli, AnEdgeWithinTheErrorIsAnErrorCode) {
    EXPECT_NE(invoke({"--json", "sqrt(0.1+0.2-0.3)"}).out.find("\"code\":\"ArgumentNearEdge\""), std::string::npos);
}

TEST(Cli, ConventionsAreOptions) {
    EXPECT_NE(invoke({"--color", "never", "--log", "e", "log(1)"}).out.find("\n= 0\n"), std::string::npos);
    EXPECT_NE(invoke({"--color", "never", "--mod", "floored", "mod(-7, 3)"}).out.find("\n= 2\n"), std::string::npos);
    EXPECT_NE(invoke({"--color", "never", "--percent", "of-value", "100+10%"}).out.find("\n= 110\n"), std::string::npos);
    EXPECT_NE(invoke({"--color", "never", "--type", "exact", "100+10%"}).out.find("\n= 1001/10 = 100.1\n"), std::string::npos);
    const Outcome bad = invoke({"--log", "2", "1"});
    EXPECT_EQ(bad.code, 2);
    EXPECT_NE(bad.err.find("--log takes 10 or e"), std::string::npos);
    EXPECT_NE(invoke({"--json", "--log", "e", "log(1)"}).out.find("\"expanded\":\"ln(1)\""), std::string::npos);
    EXPECT_NE(invoke({"--help"}).out.find("--percent"), std::string::npos);
}

TEST(Cli, CommentsAreShownWithTheirExpression) {
    const Outcome r = invoke({"--color", "never", "1+1 # two", "# a note"});
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.out.substr(0, 14), "1+1 # two\n= 2\n");
    EXPECT_NE(r.out.find("\n# a note\n"), std::string::npos);
    EXPECT_EQ(r.err, "");
    const Outcome json = invoke({"--json", "1+1 # two", "# a note"});
    EXPECT_NE(json.out.find(",\"comment\":\"two\"}\n"), std::string::npos);
    EXPECT_NE(json.out.find("{\"expression\":\"# a note\",\"type\":\"double\",\"comment\":\"a note\"}\n"), std::string::npos);
}

TEST(Cli, ConversionsFollowTheValue) {
    const Outcome r = invoke({"--color", "never", "0.1 to fraction"});
    EXPECT_EQ(r.code, 0);
    EXPECT_NE(r.out.find("\n→ 3602879701896397/36028797018963968\n  ± "), std::string::npos);
    const Outcome exact = invoke({"--type", "exact", "1/4 to fraction"});
    EXPECT_EQ(exact.out, "1/4 to fraction\n= 1/4 = 0.25\n→ 1/4\n  exact, no rounding error · κ 2e+0\n");
    const Outcome json = invoke({"--json", "0.1 to fraction"});
    EXPECT_NE(json.out.find(",\"conversion\":{\"target\":\"fraction\",\"text\":\"3602879701896397/36028797018963968\"}"),
              std::string::npos);
    EXPECT_EQ(invoke({"0.1 to nothing"}).err, "0.1 to nothing\n       ^^^^^^^ Unknown conversion 'nothing'\n");
}

TEST(Cli, TooManyTermsIsAnErrorCode) {
    EXPECT_NE(invoke({"--json", "sum(x, 1, 10001)"}).out.find("\"code\":\"TooManyTerms\""), std::string::npos);
}

TEST(Cli, NotesFollowTheReport) {
    const Outcome r = invoke({"--color", "never", "sum(x; 5; 1)"});
    EXPECT_NE(r.out.find("\n  note: sum from 5 to 1 has no terms, so it is 0\n"), std::string::npos);
    EXPECT_NE(invoke({"--json", "sum(x; 5; 1)"}).out.find(
                  ",\"warnings\":[{\"code\":\"EmptyRange\",\"message\":\"sum from 5 to 1 has no terms, so it is 0\",\"begin\":0,\"end\":12}]"),
              std::string::npos);
    EXPECT_EQ(invoke({"--json", "1+1"}).out.find("warnings"), std::string::npos);  // only when there are some
}

TEST(Cli, AssignmentsLastForTheSession) {
    const Outcome r = invoke({"--color", "never", "a := 2", "a*3"});
    EXPECT_EQ(r.code, 0);
    EXPECT_NE(r.out.find("a*3\n= 6\n"), std::string::npos);
    EXPECT_NE(invoke({"--json", "a := 2"}).out.find(",\"assigned\":\"a\""), std::string::npos);
    EXPECT_EQ(invoke({"pi := 3"}).err, "pi := 3\n^^ 'pi' is a reserved name\n");
}

TEST(Cli, AnApproximateConversionSaysHowFarItIs) {
    EXPECT_NE(invoke({"--color", "never", "2.7 to 1/3"}).out.find("\n→ 8/3 (off by 3.3e-2)\n"), std::string::npos);
    EXPECT_NE(invoke({"--json", "2.7 to 1/3"}).out.find("\"text\":\"8/3\",\"note\":\"off by 3.3e-2\"}"), std::string::npos);
    EXPECT_NE(invoke({"--color", "never", "2.5 to 1/2"}).out.find("\n→ 5/2\n"), std::string::npos);  // exact: no note
}

TEST(Cli, JsonCarriesTheReading) {
    EXPECT_NE(invoke({"--json", "2^3^2"}).out.find(",\"reading\":\"(2 ^ (3 ^ 2))\""), std::string::npos);
    EXPECT_EQ(invoke({"--color", "never", "2^3^2"}).out.find("reading"), std::string::npos);  // human output unchanged
}

// Mutation survivor (Plan 1, final checkpoint): a value meant for another convention is still refused.
TEST(Cli, AConventionRefusesAnotherConventionsValue) {
    EXPECT_EQ(invoke({"--log", "divide", "1"}).code, 2);
    EXPECT_EQ(invoke({"--mod", "e", "1"}).code, 2);
}

TEST(Cli, ListsTheFunctions) {
    const Outcome r = invoke({"--list-functions"});
    EXPECT_EQ(r.code, 0);
    EXPECT_NE(r.out.find("hypot(2)  exact  Hypotenuse\n"), std::string::npos);
    EXPECT_NE(r.out.find("log(1..2)  Logarithm\n"), std::string::npos);
    EXPECT_NE(r.out.find("mean(1..)  exact  Mean\n"), std::string::npos);
    EXPECT_NE(r.out.find("gamma(1)  Gamma function\n"), std::string::npos);
    EXPECT_NE(invoke({"--help"}).out.find("--list-functions"), std::string::npos);
}

TEST(Cli, DescribesAFunction) {
    const Outcome r = invoke({"--info", "atan2"});
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.out, "atan2(y, x) — Angle of a point\n"
                     "The angle in (−180°, 180°] of the point (x, y).\n"
                     "Example: atan2(1, -1)\n");
    const Outcome alias = invoke({"--info", "arcsen"});
    EXPECT_EQ(alias.code, 0);
    EXPECT_NE(alias.out.find("asin(x) — Inverse sine\n"), std::string::npos);
    EXPECT_NE(alias.out.find("Also: "), std::string::npos);
    EXPECT_EQ(invoke({"--info", "nosuch"}).code, 2);
    EXPECT_NE(invoke({"--list-functions"}).out.find("hypot(2)  exact  Hypotenuse\n"), std::string::npos);
}

TEST(Cli, DescribesOptionalAndRepeatedArguments) {
    EXPECT_EQ(invoke({"--info", "log"}).out.rfind("log(x[, base]) ", 0), 0u);
    EXPECT_EQ(invoke({"--info", "var"}).out.rfind("var(value, value, …) ", 0), 0u);
}

TEST(Cli, ResultsShowTheirUnit) {
    EXPECT_NE(invoke({"--color", "never", "c"}).out.find("\n= 299792458 m·s⁻¹\n"), std::string::npos);
    EXPECT_NE(invoke({"--json", "c"}).out.find(",\"unit\":\"m·s⁻¹\""), std::string::npos);
    EXPECT_EQ(invoke({"--json", "2+2"}).out.find("\"unit\""), std::string::npos);  // only when there is one
    EXPECT_NE(invoke({"--color", "never", "c+1"}).out.find("\n  note: the units of c (m·s⁻¹) and 1 (none) differ\n"),
              std::string::npos);
}
