#pragma once

#include "ast.hpp"
#include "kernels.hpp"
#include "numbers.hpp"

#include <calculate-core/calculate-core.hpp>

#include <algorithm>
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

// How a function's value responds to a small change of its arguments.
enum class Continuity {
    Continuous,  // derivatives carry the arguments' errors
    Discrete,    // whole-number valued: any uncertain argument is refused (R.4)
    Piecewise,   // continuous between jumps: refused only when the arguments' errors could reach one
};

struct FunctionInfo {
    FunctionId id;
    std::string_view name;  // spelling in the language; empty for operators and literals
    int minArgs;
    int maxArgs;            // -1: any number
    ErrorClass errorClass;
    Continuity continuity;
    bool exact;             // available for the Exact type
};

inline const FunctionInfo& functionInfo(FunctionId id) {
    using F = FunctionId;
    using C = ErrorClass;
    using K = Continuity;
    static const std::array<FunctionInfo, functionCount> table{{
        {F::Literal, "", 0, 0, C::Input, K::Continuous, true},
        {F::Pi, "pi", 0, 0, C::Input, K::Continuous, false},
        {F::E, "e", 0, 0, C::Input, K::Continuous, false},
        {F::Add, "", 2, 2, C::Checked, K::Continuous, true},
        {F::Subtract, "", 2, 2, C::Checked, K::Continuous, true},
        {F::Multiply, "", 2, 2, C::Checked, K::Continuous, true},
        {F::Divide, "", 2, 2, C::Checked, K::Continuous, true},
        {F::Negate, "", 1, 1, C::Exact, K::Continuous, true},
        {F::Power, "", 2, 2, C::Library, K::Continuous, true},
        {F::Percent, "", 1, 1, C::Checked, K::Continuous, true},
        {F::Square, "", 1, 1, C::Checked, K::Continuous, true},
        {F::Cube, "", 1, 1, C::Checked, K::Continuous, true},
        {F::Factorial, "", 1, 1, C::Counted, K::Discrete, true},
        {F::Sqrt, "sqrt", 1, 1, C::Rounded, K::Continuous, true},
        {F::Cbrt, "cbrt", 1, 1, C::Library, K::Continuous, true},
        {F::Root, "root", 2, 2, C::Library, K::Continuous, true},
        {F::Exp, "exp", 1, 1, C::Library, K::Continuous, false},
        {F::Ln, "ln", 1, 1, C::Library, K::Continuous, false},
        {F::Log10, "log", 1, 1, C::Library, K::Continuous, false},
        {F::LogBase, "log", 2, 2, C::Library, K::Continuous, false},
        {F::Sin, "sin", 1, 1, C::Library, K::Continuous, false},
        {F::Cos, "cos", 1, 1, C::Library, K::Continuous, false},
        {F::Tan, "tan", 1, 1, C::Library, K::Continuous, false},
        {F::Asin, "asin", 1, 1, C::Library, K::Continuous, false},
        {F::Acos, "acos", 1, 1, C::Library, K::Continuous, false},
        {F::Atan, "atan", 1, 1, C::Library, K::Continuous, false},
        {F::Sinh, "sinh", 1, 1, C::Library, K::Continuous, false},
        {F::Cosh, "cosh", 1, 1, C::Library, K::Continuous, false},
        {F::Tanh, "tanh", 1, 1, C::Library, K::Continuous, false},
        {F::Asinh, "asinh", 1, 1, C::Library, K::Continuous, false},
        {F::Acosh, "acosh", 1, 1, C::Library, K::Continuous, false},
        {F::Atanh, "atanh", 1, 1, C::Library, K::Continuous, false},
        {F::Abs, "abs", 1, 1, C::Exact, K::Continuous, true},
        {F::Mod, "mod", 2, 2, C::Exact, K::Piecewise, true},
        {F::Gcd, "gcd", 2, 2, C::Exact, K::Discrete, true},
        {F::Lcm, "lcm", 2, 2, C::Checked, K::Discrete, true},
        {F::Ncr, "nCr", 2, 2, C::Counted, K::Discrete, true},
        {F::Npr, "nPr", 2, 2, C::Counted, K::Discrete, true},
        {F::Median, "median", 1, -1, C::Checked, K::Continuous, true},
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
    case ErrorCode::ArgumentNearJump: return n + " jumps within the error of its arguments";
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

inline bool cancelled(const std::atomic<bool>* cancel) {
    return cancel && cancel->load(std::memory_order_relaxed);
}

// gcd, lcm, n!, nCr, nPr. gcd and lcm are exact through integers; the products count the
// multiplications and divisions that may have rounded (those past 2^p) for inexact T.
template <class T>
Applied<T> integerFunction(FunctionId id, const std::vector<T>& a, const std::atomic<bool>* cancel) {
    for (const T& x : a)
        if (!isInteger(x)) return fail<T>(ErrorCode::NotAnInteger);
    if (id == FunctionId::Gcd)
        return ok<T>(fromRational<T>(Rational(gcd(numerator(toRational(a[0])), numerator(toRational(a[1]))))));
    if (id == FunctionId::Lcm) {
        if (a[0] == 0 || a[1] == 0) return ok<T>(T(0));
        const Integer x = abs(numerator(toRational(a[0])));
        const Integer y = abs(numerator(toRational(a[1])));
        return ok<T>(fromRational<T>(Rational(x / gcd(x, y) * y)));  // gcd > 0
    }
    const T n = a[0];
    T r = id == FunctionId::Factorial ? n : a[1];
    if (n < 0 || r < 0) return fail<T>(ErrorCode::DomainError);
    if (id != FunctionId::Factorial && r > n) return ok<T>(T(0));
    if (id == FunctionId::Ncr && r > n - r) r = n - r;
    T big{};
    if constexpr (!isExact<T>) {
        using std::ldexp;
        big = ldexp(T(1), precisionBits<T>());
    }
    T v = 1;
    int roundings = 0;
    long long steps = 0;
    const auto multiply = [&](const T& factor, const T& divisor) -> std::optional<ErrorCode> {
        if (++steps % 1024 == 0 && cancelled(cancel)) return ErrorCode::Cancelled;
        v = v * factor;
        if constexpr (!isExact<T>) roundings += v >= big;
        if (divisor != 1) {
            v = v / divisor;
            if constexpr (!isExact<T>) roundings += v >= big;
        }
        if (!isFinite(v)) return ErrorCode::Overflow;
        return std::nullopt;
    };
    std::optional<ErrorCode> e;
    if (id == FunctionId::Factorial)
        for (T i = 2; !e && i <= n; i = i + 1) e = multiply(i, T(1));
    else if (id == FunctionId::Npr)
        for (T i = 0; !e && i < r; i = i + 1) e = multiply(n - i, T(1));
    else
        for (T i = 1; !e && i <= r; i = i + 1) e = multiply(n - r + i, i);
    if (e) return fail<T>(*e);
    return ok<T>(v, roundings);
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
    // n^p has at least p * msb(n) bits, so at least p * msb(n) * 0.30103 decimal digits.
    for (const Integer& n : {Integer(abs(numerator(base))), denominator(base)})  // both > 0: the base is not 0
        if (p * msb(n) * 30103 > Integer(exactDigitsLimit) * 100000) return ErrorCode::Overflow;
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
Applied<T> applyFunction(FunctionId id, const std::vector<T>& a, const std::atomic<bool>* cancel = nullptr) {
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
    case FunctionId::Abs: {
        using std::abs;
        r.value = abs(a[0]);
        break;
    }
    case FunctionId::Mod: {  // truncated, like fmod, and exact
        if (a[1] == 0) return impl::fail<T>(ErrorCode::DivisionByZero);
        const Rational x = toRational(a[0]);
        const Rational y = toRational(a[1]);
        const Rational q = x / y;
        r.value = fromRational<T>(x - y * Rational(numerator(q) / denominator(q)));
        break;
    }
    case FunctionId::Median: {
        std::vector<T> sorted = a;
        std::sort(sorted.begin(), sorted.end());
        const std::size_t n = sorted.size();
        r.value = n % 2 ? sorted[n / 2] : T((sorted[n / 2 - 1] + sorted[n / 2]) / 2);
        break;
    }
    case FunctionId::Factorial:
    case FunctionId::Ncr:
    case FunctionId::Npr:
    case FunctionId::Gcd:
    case FunctionId::Lcm:
        r = impl::integerFunction<T>(id, a, cancel);
        if (r.error) return r;
        break;
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
    using std::abs;
    using std::sqrt;
    using std::trunc;
    const R inf = std::numeric_limits<R>::infinity();
    const auto f = [](FunctionId g, const R& x) { return applyFunction<R>(g, {x}).value; };
    const R x = a.empty() ? R(0) : a[0];
    switch (id) {
    case FunctionId::Add: return {R(1), R(1)};
    case FunctionId::Subtract: return {R(1), R(-1)};
    case FunctionId::Multiply: return {a[1], a[0]};
    case FunctionId::Divide: return {R(1) / a[1], -v / a[1]};
    case FunctionId::Negate: return {R(-1)};
    case FunctionId::Percent: return {R(1) / R(100)};
    case FunctionId::Square: return {R(2) * a[0]};
    case FunctionId::Cube: return {R(3) * a[0] * a[0]};
    case FunctionId::Power: {
        const R y = a[1];
        const R dx = x == 0 ? (y == 1 ? R(1) : y > 1 ? R(0) : inf) : R(y * v / x);
        return {dx, x > 0 ? R(v * f(FunctionId::Ln, x)) : R(0)};
    }
    case FunctionId::Sqrt: return {v == 0 ? inf : R(R(1) / (2 * v))};
    case FunctionId::Cbrt: return {x == 0 ? inf : R(v / (3 * x))};
    case FunctionId::Root: {
        const R n = a[1];
        if (x == 0) return {inf, R(0)};
        return {R(v / (n * x)), R(-v * f(FunctionId::Ln, abs(x)) / (n * n))};
    }
    case FunctionId::Exp: return {v};
    case FunctionId::Ln: return {R(1) / x};
    case FunctionId::Log10: return {R(1) / (x * constantValue<R>(ConstantId::Ln10))};
    case FunctionId::LogBase: {
        const R lnb = f(FunctionId::Ln, a[1]);
        return {R(1) / (x * lnb), R(-v / (a[1] * lnb))};
    }
    case FunctionId::Sin: return {f(FunctionId::Cos, x)};
    case FunctionId::Cos: return {R(-f(FunctionId::Sin, x))};
    case FunctionId::Tan: return {R(1) + v * v};
    case FunctionId::Asin: return {abs(x) == 1 ? inf : R(R(1) / sqrt(R(1) - x * x))};
    case FunctionId::Acos: return {abs(x) == 1 ? R(-inf) : R(R(-1) / sqrt(R(1) - x * x))};
    case FunctionId::Atan: return {R(1) / (R(1) + x * x)};
    case FunctionId::Sinh: return {f(FunctionId::Cosh, x)};
    case FunctionId::Cosh: return {f(FunctionId::Sinh, x)};
    case FunctionId::Tanh: return {R(1) - v * v};
    case FunctionId::Asinh: return {R(1) / sqrt(x * x + 1)};
    case FunctionId::Acosh: return {x == 1 ? inf : R(R(1) / sqrt(x * x - 1))};
    case FunctionId::Atanh: return {R(1) / (R(1) - x * x)};
    case FunctionId::Abs: return {x < 0 ? R(-1) : R(1)};
    case FunctionId::Mod: return {R(1), R(-trunc(a[0] / a[1]))};
    case FunctionId::Median: {  // the selected element (or the two middle ones) gets the weight
        std::vector<int> order(a.size());
        for (std::size_t i = 0; i < a.size(); ++i) order[i] = static_cast<int>(i);
        std::stable_sort(order.begin(), order.end(), [&a](int i, int j) { return a[i] < a[j]; });
        std::vector<R> d(a.size(), R(0));
        const std::size_t n = a.size();
        if (n % 2) d[order[n / 2]] = 1;
        else d[order[n / 2 - 1]] = d[order[n / 2]] = R(0.5);
        return d;
    }
    default: return std::vector<R>(a.size(), R(0));  // discrete functions and constants
    }
}

namespace impl {

// f(x) in the ruler's arithmetic, infinite where it cannot be computed (a slope may then only be too large).
inline Ruler rulerValue(FunctionId id, const std::vector<Ruler>& a) {
    const Applied<Ruler> r = applyFunction<Ruler>(id, a);
    return r.error ? std::numeric_limits<Ruler>::infinity() : r.value;
}

// x^y: x moves with y at its value, then y moves with x anywhere in its interval.
inline std::vector<Ruler> powerSlopes(const std::vector<Ruler>& a, const std::vector<Ruler>& b) {
    using std::abs;
    const Ruler inf = std::numeric_limits<Ruler>::infinity();
    const Ruler& y = a[1];
    const Ruler lo = a[0] - b[0];
    const Ruler hi = a[0] + b[0];
    // d/dx = y * x^(y-1): |x|^(y-1) grows with |x| when y >= 1 and shrinks when y < 1. A fractional y needs x >= 0.
    Ruler dx;
    if (y == 0) dx = 0;
    else if (!isInteger(y) && lo < 0) dx = inf;
    else if (y >= 1) dx = abs(y) * rulerValue(FunctionId::Power, {abs(a[0]) + b[0], y - 1});
    else if (abs(a[0]) > b[0]) dx = abs(y) * rulerValue(FunctionId::Power, {abs(a[0]) - b[0], y - 1});
    else dx = inf;
    // d/dy = x^y * ln x. A negative base exists only at whole exponents, where no slope carries the exponent's error.
    Ruler dy;
    if (hi <= 0) dy = 0;
    else if (lo <= 0) dy = inf;
    else {
        Ruler top = 0;  // x^y is monotone in x and in y, so its largest value is at a corner
        for (const Ruler& x : {lo, hi})
            for (const Ruler& e : {Ruler(y - b[1]), Ruler(y + b[1])})
                top = std::max(top, rulerValue(FunctionId::Power, {x, e}));
        dy = top * std::max(abs(rulerValue(FunctionId::Ln, {lo})), abs(rulerValue(FunctionId::Ln, {hi})));
    }
    return {dx, dy};
}

// root(x, n) = x^(1/n): x moves with n at its value, then n moves with x anywhere in its interval.
inline std::vector<Ruler> rootSlopes(const std::vector<Ruler>& a, const std::vector<Ruler>& b) {
    using std::abs;
    const Ruler inf = std::numeric_limits<Ruler>::infinity();
    const Ruler far = abs(a[0]) + b[0];   // the largest |x| in its interval
    const Ruler near = abs(a[0]) - b[0];  // the smallest, when the interval stays clear of 0
    // d/dx = |x|^(1/n - 1) / |n|: grows with |x| when 1/n >= 1, shrinks otherwise.
    const Ruler e = 1 / a[1] - 1;
    Ruler dx = inf;
    if (e >= 0) dx = rulerValue(FunctionId::Power, {far, e}) / abs(a[1]);
    else if (near > 0) dx = rulerValue(FunctionId::Power, {near, e}) / abs(a[1]);
    // d/dn = -x^(1/n) * ln|x| / n^2: at most the largest |x|^(1/n) (at a corner: it is monotone in |x| and in 1/n)
    // times the largest |ln|x||, over the smallest n^2.
    const Ruler nNear = abs(a[1]) - b[1];
    Ruler dn = inf;
    if (near > 0 && nNear > 0) {
        Ruler top = 0;
        for (const Ruler& m : {near, far})
            for (const Ruler& n : {Ruler(a[1] - b[1]), Ruler(a[1] + b[1])})
                top = std::max(top, rulerValue(FunctionId::Power, {m, Ruler(1 / n)}));
        const Ruler ln = std::max(abs(rulerValue(FunctionId::Ln, {near})), abs(rulerValue(FunctionId::Ln, {far})));
        dn = top * ln / (nNear * nNear);
    }
    return {dx, dn};
}

// log(x, b) = ln x / ln b: x moves with b at its value, then b moves with x anywhere in its interval.
inline std::vector<Ruler> logBaseSlopes(const std::vector<Ruler>& a, const std::vector<Ruler>& b) {
    using std::abs;
    const Ruler inf = std::numeric_limits<Ruler>::infinity();
    const auto ln = [](const Ruler& t) { return rulerValue(FunctionId::Ln, {t}); };
    const Ruler lo = a[0] - b[0];
    const Ruler baseLo = a[1] - b[1];
    const Ruler baseHi = a[1] + b[1];
    const Ruler dx = lo > 0 ? Ruler(1 / (lo * abs(ln(a[1])))) : inf;
    // d/db = -ln x / (b * ln^2 b): the largest |ln x| over the smallest b and the smallest ln^2 b (at b's end
    // nearest 1). Unbounded where b's interval reaches 0 or 1.
    Ruler db = inf;
    if (lo > 0 && baseLo > 0 && (baseHi < 1 || baseLo > 1)) {
        const Ruler nearOne = baseHi < 1 ? abs(ln(baseHi)) : ln(baseLo);
        db = std::max(abs(ln(lo)), abs(ln(a[0] + b[0]))) / (baseLo * nearOne * nearOne);
    }
    return {dx, db};
}

// One-argument functions: the largest |f'| over [x - b, x + b]. Each f' there is monotone in x or in |x| (or, for
// sin and cos, 1-Lipschitz), so its largest value is at an end of the interval, or at the |x| farthest from or
// nearest to 0.
inline Ruler functionSlope(FunctionId id, const Ruler& x, const Ruler& b) {
    using std::abs;
    using std::floor;
    using std::sqrt;
    const Ruler inf = std::numeric_limits<Ruler>::infinity();
    const auto f = [](FunctionId g, const Ruler& t) { return rulerValue(g, {t}); };
    const Ruler lo = x - b;
    const Ruler hi = x + b;
    const Ruler far = abs(x) + b;                                  // the largest |x| in the interval
    const Ruler near = abs(x) > b ? Ruler(abs(x) - b) : Ruler(0);  // the smallest
    switch (id) {
    case FunctionId::Sqrt: return lo > 0 ? Ruler(1 / (2 * sqrt(lo))) : inf;
    case FunctionId::Cbrt: {
        const Ruler c = f(FunctionId::Cbrt, near);
        return near > 0 ? Ruler(1 / (3 * c * c)) : inf;
    }
    case FunctionId::Exp: return f(FunctionId::Exp, hi);
    case FunctionId::Ln: return lo > 0 ? Ruler(1 / lo) : inf;
    case FunctionId::Log10: return lo > 0 ? Ruler(1 / (lo * constantValue<Ruler>(ConstantId::Ln10))) : inf;
    case FunctionId::Sin: return std::min(Ruler(1), Ruler(abs(f(FunctionId::Cos, x)) + b));
    case FunctionId::Cos: return std::min(Ruler(1), Ruler(abs(f(FunctionId::Sin, x)) + b));
    case FunctionId::Tan: {  // 1 + tan^2: unbounded at the poles (k + 1/2)pi, else largest at an end
        const Ruler pi = constantValue<Ruler>(ConstantId::Pi);
        const Ruler pole = (floor(hi / pi - Ruler(0.5)) + Ruler(0.5)) * pi;  // the last pole at or below hi
        if (pole >= lo) return inf;
        const Ruler t = std::max(abs(f(FunctionId::Tan, lo)), abs(f(FunctionId::Tan, hi)));
        return 1 + t * t;
    }
    case FunctionId::Asin:
    case FunctionId::Acos: return far < 1 ? Ruler(1 / sqrt(1 - far * far)) : inf;
    case FunctionId::Atan: return 1 / (1 + near * near);
    case FunctionId::Sinh: return f(FunctionId::Cosh, far);
    case FunctionId::Cosh: return f(FunctionId::Sinh, far);
    case FunctionId::Tanh: {  // 1 - tanh^2, written 1 / cosh^2 to keep its precision when it is tiny
        const Ruler c = f(FunctionId::Cosh, near);
        return 1 / (c * c);
    }
    case FunctionId::Asinh: return 1 / sqrt(near * near + 1);
    case FunctionId::Acosh: return lo > 1 ? Ruler(1 / sqrt(lo * lo - 1)) : inf;
    case FunctionId::Atanh: return far < 1 ? Ruler(1 / (1 - far * far)) : inf;
    default: return inf;  // not a one-argument function
    }
}

}  // namespace impl

// Upper bounds of |d f / d arg_k| over the arguments' error intervals [a_j - b_j, a_j + b_j], one argument at a
// time: argument k and those before it anywhere in their intervals, those after it at their computed values. The
// mean value theorem, applied to one argument after another, gives |f(computed) - f(exact)| <= sum_k slope_k * b_k
// with nothing dropped. Infinite where an interval reaches a point where the derivative is unbounded or f undefined.
inline std::vector<Ruler> slopes(FunctionId id, const std::vector<Ruler>& a, const std::vector<Ruler>& b) {
    using std::abs;
    using std::floor;
    const Ruler inf = std::numeric_limits<Ruler>::infinity();
    switch (id) {
    case FunctionId::Add:
    case FunctionId::Subtract: return {Ruler(1), Ruler(1)};
    case FunctionId::Negate:
    case FunctionId::Abs: return {Ruler(1)};
    case FunctionId::Percent: return {Ruler(1) / 100};
    case FunctionId::Multiply: return {abs(a[1]), abs(a[0]) + b[0]};
    case FunctionId::Divide: {
        const Ruler nearest = abs(a[1]) - b[1];  // the smallest |y| in its interval
        return {Ruler(1) / abs(a[1]), nearest > 0 ? Ruler((abs(a[0]) + b[0]) / (nearest * nearest)) : inf};
    }
    case FunctionId::Square: return {2 * (abs(a[0]) + b[0])};
    case FunctionId::Cube: return {3 * (abs(a[0]) + b[0]) * (abs(a[0]) + b[0])};
    case FunctionId::Power: return impl::powerSlopes(a, b);
    case FunctionId::Mod: {  // between its jumps: |trunc(x/y)| is largest at the largest |x| over the smallest |y|
        const Ruler nearest = abs(a[1]) - b[1];
        return {Ruler(1), nearest > 0 ? Ruler(floor((abs(a[0]) + b[0]) / nearest)) : inf};
    }
    case FunctionId::Root: return impl::rootSlopes(a, b);
    case FunctionId::LogBase: return impl::logBaseSlopes(a, b);
    case FunctionId::Sqrt:
    case FunctionId::Cbrt:
    case FunctionId::Exp:
    case FunctionId::Ln:
    case FunctionId::Log10:
    case FunctionId::Sin:
    case FunctionId::Cos:
    case FunctionId::Tan:
    case FunctionId::Asin:
    case FunctionId::Acos:
    case FunctionId::Atan:
    case FunctionId::Sinh:
    case FunctionId::Cosh:
    case FunctionId::Tanh:
    case FunctionId::Asinh:
    case FunctionId::Acosh:
    case FunctionId::Atanh: return {impl::functionSlope(id, a[0], b[0])};
    default: {  // the median's weights at the computed arguments; the discrete functions' zeros
        std::vector<Ruler> d = partials<Ruler>(id, a, Ruler(0));  // neither uses the value
        for (Ruler& x : d) x = abs(x);
        return d;
    }
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
    case FunctionId::Lcm: {
        if (a[0] == 0 || a[1] == 0) return Rational(0);
        const Integer x = abs(numerator(a[0]));
        const Integer y = abs(numerator(a[1]));
        return Rational(x / gcd(x, y) * y);
    }
    case FunctionId::Median: {
        std::vector<Rational> sorted = a;
        std::sort(sorted.begin(), sorted.end());
        const std::size_t n = sorted.size();
        return n % 2 ? sorted[n / 2] : Rational((sorted[n / 2 - 1] + sorted[n / 2]) / 2);
    }
    default: return std::nullopt;
    }
}

// Whether a computed root is exact: value^n == x, compared as rationals.
template <class T>
bool exactRootResult(FunctionId id, const std::vector<T>& a, const T& value) {
    using std::abs;
    const Rational v = toRational(value);
    const Rational x = toRational(a[0]);
    if (id == FunctionId::Sqrt) return v * v == x;
    if (id == FunctionId::Cbrt) return v * v * v == x;
    if (id == FunctionId::Root && isInteger(a[1]) && a[1] != 0 && abs(a[1]) <= 64) {
        const long long n = toLongLong(a[1]);
        const unsigned e = static_cast<unsigned>(n < 0 ? -n : n);
        if (v == 0) return false;
        const Rational power(pow(numerator(v), e), pow(denominator(v), e));
        return n > 0 ? power == x : 1 / power == x;
    }
    return false;
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
        std::vector<Rational> exactArgs;
        for (const T& x : args) exactArgs.push_back(toRational(x));
        if (id == FunctionId::Power && impl::isInteger(args[1]) && abs(args[1]) <= 64) {  // checked exactly
            Rational exact;
            if (!impl::exactPower(exactArgs[0], exactArgs[1], exact))
                return fromRational<Ruler>(abs(exact - toRational(applied.value)));
        }
        if (impl::exactRootResult(id, args, applied.value)) return Ruler(0);
        switch (functionInfo(id).errorClass) {
        case ErrorClass::Exact: return Ruler(0);
        case ErrorClass::Checked: {
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
