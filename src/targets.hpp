#pragma once

#include "engine.hpp"
#include "parser.hpp"

#include <calculate-core/calculate-core.hpp>

#include <optional>
#include <string_view>
#include <vector>

namespace calculate_core::detail {

// What a target gets: the expression, how it was evaluated, and its result exactly.
struct TargetInput {
    const Parsed& parsed;    // the expression; parsed.target holds the name and its argument
    const Options& options;  // the number type and angle it was evaluated in
    const Rational& value;   // the result as stored in that type, exactly
    const Report& report;    // its error report, in the ruler's precision
};

// Fills result.conversion (and anything else the target shows), or says why it can't.
using TargetFunction = std::optional<Error> (*)(const TargetInput& in, Result& result);

struct Target {
    std::string_view name;     // as written after `to`
    std::string_view summary;  // one line, for listings
    TargetFunction apply;
};

// Every target, in listing order. To add one, add a row and its function in targets.cpp.
const std::vector<Target>& targets();
const Target* findTarget(std::string_view name);  // nullptr when there is none

}  // namespace calculate_core::detail
