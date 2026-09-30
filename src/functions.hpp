#pragma once

#include "ast.hpp"
#include "kernels.hpp"
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

template <class T>
struct Applied {
    T value{};
    std::optional<ErrorCode> error;
    int roundings = 0;  // Counted functions only
};

// Every Library function claims |computed - exact| <= claim * u * max(|v|, min()) (in units of u).
inline int claimedFactor(FunctionId) { return 2; }

namespace impl {

template <class T>
Applied<T> fail(ErrorCode code) {
    Applied<T> r;
    r.error = code;
    return r;
}

template <class T>
Applied<T> ok(const T& value, int roundings = 0) {
    Applied<T> r;
    r.value = value;
    r.roundings = roundings;
    return r;
}

template <class T>
T withSign(const T& v, bool negative) {
    return negative ? T(-v) : v;
}

// The elementary functions, for inexact T: every result computed in T through double words.
template <class T>
Applied<T> kernel(FunctionId id, const std::vector<T>& a) {
    const T x = a[0];
    const auto fromExp = [](const ExpParts<T>& e, bool negative) -> Applied<T> {
        if (e.overflow) return fail<T>(ErrorCode::Overflow);
        if (e.underflow) return ok<T>(T(0));
        return ok<T>(withSign(toValue(expValue(e)), negative));
    };
    switch (id) {
    case FunctionId::Exp: return fromExp(expParts(dw(x)), false);
    default: return fail<T>(ErrorCode::DomainError);
    }
}

}  // namespace impl

// One node computed in T. Errors are values: never NaN or infinity.
template <class T>
Applied<T> applyFunction(FunctionId id, const std::vector<T>& a, const std::atomic<bool>* /*cancel*/ = nullptr) {
    Applied<T> r;
    switch (id) {
    case FunctionId::Pi:
    case FunctionId::E:
        if constexpr (isExact<T>) r.error = ErrorCode::NotAvailableInExact;
        else r.value = constantValue<T>(id == FunctionId::Pi ? ConstantId::Pi : ConstantId::E);
        return r;
    case FunctionId::Add: r.value = a[0] + a[1]; break;
    case FunctionId::Subtract: r.value = a[0] - a[1]; break;
    case FunctionId::Multiply: r.value = a[0] * a[1]; break;
    case FunctionId::Divide:
        if (a[1] == 0) {  // before dividing: Boost's Rational would silently give 0
            r.error = ErrorCode::DivisionByZero;
            return r;
        }
        r.value = a[0] / a[1];
        break;
    case FunctionId::Negate: r.value = -a[0]; break;
    case FunctionId::Percent: r.value = a[0] / T(100); break;
    case FunctionId::Square: r.value = a[0] * a[0]; break;
    case FunctionId::Cube: r.value = a[0] * a[0] * a[0]; break;
    default:
        if constexpr (isExact<T>) {
            if (!functionInfo(id).exact) return impl::fail<T>(ErrorCode::NotAvailableInExact);
            return impl::fail<T>(ErrorCode::DomainError);
        } else {
            r = impl::kernel<T>(id, a);
            if (r.error) return r;
        }
    }
    if (!isFinite(r.value)) r.error = ErrorCode::Overflow;
    return r;
}

// d f / d arg_k at the computed arguments, in any type R.
template <class R>
std::vector<R> partials(FunctionId id, const std::vector<R>& a, const R& v) {
    switch (id) {
    case FunctionId::Add: return {R(1), R(1)};
    case FunctionId::Subtract: return {R(1), R(-1)};
    case FunctionId::Multiply: return {a[1], a[0]};
    case FunctionId::Divide: return {R(1) / a[1], -v / a[1]};
    case FunctionId::Negate: return {R(-1)};
    case FunctionId::Percent: return {R(1) / R(100)};
    case FunctionId::Square: return {R(2) * a[0]};
    case FunctionId::Cube: return {R(3) * a[0] * a[0]};
    default: return std::vector<R>(a.size(), R(0));
    }
}

namespace impl {

// The exact result of an operation on exact arguments, where rational arithmetic can give it.
inline std::optional<Rational> exactResult(FunctionId id, const std::vector<Rational>& a) {
    switch (id) {
    case FunctionId::Add: return a[0] + a[1];
    case FunctionId::Subtract: return a[0] - a[1];
    case FunctionId::Multiply: return a[0] * a[1];
    case FunctionId::Divide: return a[1] == 0 ? std::optional<Rational>() : a[0] / a[1];
    case FunctionId::Percent: return a[0] / 100;
    case FunctionId::Square: return a[0] * a[0];
    case FunctionId::Cube: return a[0] * a[0] * a[0];
    default: return std::nullopt;
    }
}

}  // namespace impl

// A bound on |exact f(args) - computed value| for one node, with its arguments taken as exact.
template <class T>
Ruler localError(FunctionId id, const std::vector<T>& args, const Applied<T>& applied) {
    if constexpr (isExact<T>) {
        return Ruler(0);
    } else {
        using std::abs;
        const Ruler u = exactCast<Ruler>(unitRoundoff<T>());
        const Ruler v = abs(exactCast<Ruler>(applied.value));
        switch (functionInfo(id).errorClass) {
        case ErrorClass::Exact: return Ruler(0);
        case ErrorClass::Checked: {
            std::vector<Rational> exactArgs;
            for (const T& x : args) exactArgs.push_back(toRational(x));
            const auto exact = impl::exactResult(id, exactArgs);
            return exact ? fromRational<Ruler>(abs(*exact - toRational(applied.value))) : Ruler(0);
        }
        case ErrorClass::Rounded: return u * v;
        case ErrorClass::Counted: return Ruler(applied.roundings) * u * v;
        case ErrorClass::Library: {
            const Ruler floor = exactCast<Ruler>((std::numeric_limits<T>::min)());
            return Ruler(claimedFactor(id)) * u * (v > floor ? v : floor);
        }
        default: return Ruler(0);  // Input errors belong to the engine
        }
    }
}

}  // namespace calculate_core::detail
