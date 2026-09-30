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
