#pragma once

#include <calculate-core/calculate-core.hpp>

#include <iosfwd>
#include <string>
#include <vector>

namespace calc {

// Runs the command line: args without the program name. `terminal` is whether stdout is a terminal
// (it decides --color=auto). Returns the exit code: 0 success, 1 an expression failed, 2 usage error.
int run(const std::vector<std::string>& args, std::istream& in, std::ostream& out, std::ostream& err, bool terminal);

// "0.3000000000000000|444089…": every digit, with a bar after the trusted ones (the non-colour cue)
// and the noise dimmed when `color` is set.
std::string formatValue(const calculate_core::Digits& value, int trustedDigits, bool color);

// "1/3 = 0.(3)", "3/8 = 0.375", "-54767/66192", "10".
std::string formatFraction(const calculate_core::Fraction& f);

// A JSON string literal with escapes.
std::string jsonString(const std::string& s);

}  // namespace calc
