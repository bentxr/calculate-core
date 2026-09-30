#pragma once

#include "ast.hpp"
#include "kernels.hpp"
#include "numbers.hpp"

#include <calculate-core/calculate-core.hpp>

#include <array>
#include <atomic>
#include <limits>
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

template <class T>
bool isInteger(const T& x) {
    if constexpr (isExact<T>) {
        return denominator(x) == 1;
    } else {
        using std::trunc;
        return trunc(x) == x;
    }
}

// x must be an integer. Values of at least 2^p are all even.
template <class T>
bool isOdd(const T& x) {
    if constexpr (isExact<T>) {
        return (numerator(x) & 1) != 0;
    } else {
        using std::abs;
        using std::ldexp;
        using std::trunc;
        if (abs(x) >= ldexp(T(1), precisionBits<T>())) return false;
        return trunc(x / 2) * 2 != x;
    }
}

// n >= 0: the integer q-th root of n, if n is a perfect q-th power (Newton's method from above).
inline std::optional<Integer> integerRoot(const Integer& n, unsigned q) {
    if (n < 2 || q == 1) return n;
    Integer y = pow(Integer(2), (static_cast<unsigned>(msb(n)) + q) / q);  // >= the root
    for (;;) {
        const Integer next = ((q - 1) * y + n / pow(y, q - 1)) / q;
        if (next >= y) break;
        y = next;
    }
    if (pow(y, q) == n) return y;
    return std::nullopt;
}

// x^(1/q) exactly, or the reason it is not rational.
inline std::optional<ErrorCode> exactRoot(const Rational& x, const Integer& q, Rational& out) {
    if (x == 0 || x == 1) {
        out = x;
        return std::nullopt;
    }
    const bool negative = x < 0;
    if (negative && (q & 1) == 0) return ErrorCode::DomainError;
    if (q > (1u << 20)) return ErrorCode::IrrationalResult;  // no rational root of that order matters
    const unsigned order = q.convert_to<unsigned>();
    const auto n = integerRoot(abs(numerator(x)), order);
    const auto d = integerRoot(denominator(x), order);
    if (!n || !d) return ErrorCode::IrrationalResult;
    out = Rational(*n, *d);  // *d >= 1
    if (negative) out = -out;
    return std::nullopt;
}

inline std::optional<ErrorCode> exactPower(const Rational& x, const Rational& y, Rational& out) {
    if (y == 0) {
        out = 1;
        return std::nullopt;
    }
    if (x == 0) {
        if (y < 0) return ErrorCode::DivisionByZero;
        out = 0;
        return std::nullopt;
    }
    Rational base = x;
    if (denominator(y) != 1)
        if (const auto e = exactRoot(x, denominator(y), base)) return e;
    const Integer p = abs(numerator(y));
    if (p > std::numeric_limits<unsigned>::max()) return ErrorCode::Overflow;  // cannot be stored
    const unsigned e = p.convert_to<unsigned>();
    out = Rational(pow(numerator(base), e), pow(denominator(base), e));
    if (y < 0) out = 1 / out;  // base != 0
    return std::nullopt;
}

// Roots and powers in the Exact type: exact when the result is rational, an error otherwise.
template <class T>
Applied<T> exactFunction(FunctionId id, const std::vector<Rational>& a) {
    Rational out;
    std::optional<ErrorCode> e;
    switch (id) {
    case FunctionId::Power: e = exactPower(a[0], a[1], out); break;
    case FunctionId::Sqrt: e = exactRoot(a[0], Integer(2), out); break;
    case FunctionId::Cbrt: e = exactRoot(a[0], Integer(3), out); break;
    case FunctionId::Root:
        if (a[1] == 0) return fail<T>(ErrorCode::DomainError);
        e = exactPower(a[0], 1 / a[1], out);
        break;
    default: return fail<T>(ErrorCode::NotAvailableInExact);
    }
    if (e) return fail<T>(*e);
    return ok<T>(out);
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
    case FunctionId::Sqrt: {
        using std::sqrt;
        if (x < 0) return fail<T>(ErrorCode::DomainError);
        return ok<T>(sqrt(x));  // IEEE: correctly rounded on every platform
    }
    case FunctionId::Power: {  // IEEE pow rules
        using std::abs;
        const T y = a[1];
        if (y == 0 || x == 1) return ok<T>(T(1));
        if (x == 0) return y > 0 ? ok<T>(T(0)) : fail<T>(ErrorCode::DivisionByZero);
        if (x < 0 && !isInteger(y)) return fail<T>(ErrorCode::DomainError);
        return fromExp(expParts(logWord(dw(abs(x))) * y), x < 0 && isOdd(y));
    }
    case FunctionId::Cbrt: {
        using std::abs;
        if (x == 0) return ok<T>(x);
        return fromExp(expParts(logWord(dw(abs(x))) / T(3)), x < 0);
    }
    case FunctionId::Root: {
        using std::abs;
        const T n = a[1];
        if (n == 0) return fail<T>(ErrorCode::DomainError);
        if (x == 0) return n > 0 ? ok<T>(T(0)) : fail<T>(ErrorCode::DivisionByZero);
        if (x < 0 && !(isInteger(n) && isOdd(n))) return fail<T>(ErrorCode::DomainError);
        return fromExp(expParts(logWord(dw(abs(x))) / n), x < 0);
    }
    case FunctionId::Ln:
    case FunctionId::Log10:
        if (x <= 0) return fail<T>(ErrorCode::DomainError);
        if (id == FunctionId::Ln) return ok<T>(toValue(logWord(dw(x))));
        return ok<T>(toValue(logWord(dw(x)) / impl::word<T>(ConstantId::Ln10)));
    case FunctionId::LogBase:
        if (x <= 0 || a[1] <= 0 || a[1] == 1) return fail<T>(ErrorCode::DomainError);
        return ok<T>(toValue(logWord(dw(x)) / logWord(dw(a[1]))));
    case FunctionId::Sin:
    case FunctionId::Cos:
    case FunctionId::Tan: {
        using std::abs;
        const auto reduced = reduceHalfPi(abs(x));
        if (!reduced) return fail<T>(ErrorCode::ArgumentTooLarge);
        const int q = reduced->quadrant;
        const DoubleWord<T> s = sinSmall(reduced->r);
        const DoubleWord<T> c = cosSmall(reduced->r);
        if (id == FunctionId::Sin) return ok<T>(withSign(toValue(q == 0 ? s : q == 1 ? c : q == 2 ? -s : -c), x < 0));
        if (id == FunctionId::Cos) return ok<T>(toValue(q == 0 ? c : q == 1 ? -s : q == 2 ? -c : s));
        return ok<T>(withSign(toValue(q % 2 == 0 ? s / c : -(c / s)), x < 0));
    }
    case FunctionId::Asin:
    case FunctionId::Acos: {
        using std::abs;
        if (abs(x) > 1) return fail<T>(ErrorCode::DomainError);
        return ok<T>(toValue(id == FunctionId::Asin ? asinWord(dw(x)) : acosWord(dw(x))));
    }
    case FunctionId::Atan: return ok<T>(toValue(atanWord(dw(x))));
    case FunctionId::Sinh:
    case FunctionId::Cosh: {
        using std::abs;
        const T ax = abs(x);
        const bool sinh = id == FunctionId::Sinh;
        const DoubleWord<T>& ln2 = impl::word<T>(ConstantId::Ln2);
        if (ax > T(precisionBits<T>() + 2) * ln2.hi / 2)  // e^-|x| is negligible: the result is e^|x| / 2
            return fromExp(expParts(dw(ax) - ln2), sinh && x < 0);
        if (ax < 1) {
            DoubleWord<T> m = expm1Small(dw(ax / 4));  // expm1(|x|) by two doublings
            m = m * (m + T(2));
            m = m * (m + T(2));
            if (sinh) return ok<T>(withSign(toValue(scale(m + m / (m + T(1)), -1)), x < 0));
            return ok<T>(toValue(scale(m * m / (m + T(1)), -1) + T(1)));
        }
        const DoubleWord<T> y = expValue(expParts(dw(ax)));
        const DoubleWord<T> inverse = dw(T(1)) / y;
        return ok<T>(withSign(toValue(scale(sinh ? y - inverse : y + inverse, -1)), sinh && x < 0));
    }
    case FunctionId::Tanh: {
        using std::abs;
        using std::ldexp;
        const T ax = abs(x);
        if (ax > T(precisionBits<T>() + 3) * impl::word<T>(ConstantId::Ln2).hi / 2) return ok<T>(withSign(T(1), x < 0));
        const DoubleWord<T> m = expValue(expParts(dw(ldexp(ax, 1)))) - T(1);  // expm1(2|x|)
        return ok<T>(withSign(toValue(m / (m + T(2))), x < 0));
    }
    case FunctionId::Asinh: {
        using std::abs;
        using std::ldexp;
        const T ax = abs(x);
        if (ax > ldexp(T(1), precisionBits<T>() / 2 + 2))  // asinh = ln(2|x|)
            return ok<T>(withSign(toValue(logWord(dw(ax)) + impl::word<T>(ConstantId::Ln2)), x < 0));
        const DoubleWord<T> a2 = dw(ax) * ax;
        const DoubleWord<T> w = a2 / (sqrt(a2 + T(1)) + T(1)) + ax;  // asinh = log1p(w)
        return ok<T>(withSign(toValue(logWord(w + T(1))), x < 0));
    }
    case FunctionId::Acosh: {
        using std::ldexp;
        if (x < 1) return fail<T>(ErrorCode::DomainError);
        if (x > ldexp(T(1), precisionBits<T>() / 2 + 2))  // acosh = ln(2x)
            return ok<T>(toValue(logWord(dw(x)) + impl::word<T>(ConstantId::Ln2)));
        const DoubleWord<T> t = dw(x) - T(1);
        return ok<T>(toValue(logWord(t + sqrt(t * (t + T(2))) + T(1))));
    }
    case FunctionId::Atanh: {
        using std::abs;
        const T ax = abs(x);
        if (ax >= 1) return fail<T>(ErrorCode::DomainError);
        const DoubleWord<T> w = scale(dw(ax), 1) / (dw(T(1)) - ax);  // atanh = log1p(w) / 2
        return ok<T>(withSign(toValue(scale(logWord(w + T(1)), -1)), x < 0));
    }
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
            return impl::exactFunction<T>(id, a);
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
