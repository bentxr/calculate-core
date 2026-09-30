#pragma once

#include "ast.hpp"
#include "numbers.hpp"

#include <calculate-core/calculate-core.hpp>

#include <array>
#include <atomic>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace calculate_core::detail {

// How a node's own (local) error is known.
enum class ErrorClass {
    Input,     // literals and constants: representation error, known exactly
    Exact,     // the operation never rounds
    Checked,   // exact local error from exact rational arithmetic
    Rounded,   // correctly rounded irrational result: at most u|v|
    Counted,   // a sequence of roundings: at most roundings * u|v|
    Library,   // an elementary kernel: at most claim * u * max(|v|, min)
};

struct FunctionInfo {
    FunctionId id;
    std::string_view name;  // spelling in the language; empty for operators and literals
    int minArgs;
    int maxArgs;            // -1: any number
    ErrorClass errorClass;
    bool discrete;          // argument uncertainty cannot propagate through it
    bool exact;             // available for the Exact type
};

inline const FunctionInfo& functionInfo(FunctionId id) {
    using F = FunctionId;
    using C = ErrorClass;
    static const std::array<FunctionInfo, functionCount> table{{
        {F::Literal, "", 0, 0, C::Input, false, true},
        {F::Pi, "pi", 0, 0, C::Input, false, false},
        {F::E, "e", 0, 0, C::Input, false, false},
        {F::Add, "", 2, 2, C::Checked, false, true},
        {F::Subtract, "", 2, 2, C::Checked, false, true},
        {F::Multiply, "", 2, 2, C::Checked, false, true},
        {F::Divide, "", 2, 2, C::Checked, false, true},
        {F::Negate, "", 1, 1, C::Exact, false, true},
        {F::Power, "", 2, 2, C::Library, false, true},
        {F::Percent, "", 1, 1, C::Checked, false, true},
        {F::Square, "", 1, 1, C::Checked, false, true},
        {F::Cube, "", 1, 1, C::Checked, false, true},
        {F::Factorial, "", 1, 1, C::Counted, true, true},
        {F::Sqrt, "sqrt", 1, 1, C::Rounded, false, true},
        {F::Cbrt, "cbrt", 1, 1, C::Library, false, true},
        {F::Root, "root", 2, 2, C::Library, false, true},
        {F::Exp, "exp", 1, 1, C::Library, false, false},
        {F::Ln, "ln", 1, 1, C::Library, false, false},
        {F::Log10, "log", 1, 1, C::Library, false, false},
        {F::LogBase, "log", 2, 2, C::Library, false, false},
        {F::Sin, "sin", 1, 1, C::Library, false, false},
        {F::Cos, "cos", 1, 1, C::Library, false, false},
        {F::Tan, "tan", 1, 1, C::Library, false, false},
        {F::Asin, "asin", 1, 1, C::Library, false, false},
        {F::Acos, "acos", 1, 1, C::Library, false, false},
        {F::Atan, "atan", 1, 1, C::Library, false, false},
        {F::Sinh, "sinh", 1, 1, C::Library, false, false},
        {F::Cosh, "cosh", 1, 1, C::Library, false, false},
        {F::Tanh, "tanh", 1, 1, C::Library, false, false},
        {F::Asinh, "asinh", 1, 1, C::Library, false, false},
        {F::Acosh, "acosh", 1, 1, C::Library, false, false},
        {F::Atanh, "atanh", 1, 1, C::Library, false, false},
        {F::Abs, "abs", 1, 1, C::Exact, false, true},
        {F::Mod, "mod", 2, 2, C::Exact, false, true},
        {F::Gcd, "gcd", 2, 2, C::Exact, true, true},
        {F::Lcm, "lcm", 2, 2, C::Checked, true, true},
        {F::Ncr, "nCr", 2, 2, C::Counted, true, true},
        {F::Npr, "nPr", 2, 2, C::Counted, true, true},
        {F::Median, "median", 1, -1, C::Checked, false, true},
    }};
    return table[static_cast<std::size_t>(id)];
}

// English messages; `name` is the function's name and may be empty.
inline std::string errorMessage(ErrorCode code, std::string_view name) {
    const std::string n(name);
    switch (code) {
    case ErrorCode::DivisionByZero: return "Division by zero";
    case ErrorCode::Overflow: return "The result is too large for this number type";
    case ErrorCode::LiteralOutOfRange: return "This number cannot be represented in this number type";
    case ErrorCode::NotAvailableInExact: return n + " is not available in exact arithmetic: its result is irrational";
    case ErrorCode::DomainError: return n + " is not defined for this argument";
    case ErrorCode::IrrationalResult: return "The exact result of " + n + " is irrational";
    case ErrorCode::ArgumentTooLarge: return "The argument of " + n + " is too large to reduce accurately";
    case ErrorCode::NotAnInteger: return n + " needs a whole-number argument";
    case ErrorCode::UncertainDiscreteArgument: return n + " needs an exactly known argument";
    case ErrorCode::Cancelled: return "Cancelled";
    default: return "Invalid expression";
    }
}

}  // namespace calculate_core::detail
