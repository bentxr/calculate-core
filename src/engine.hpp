#pragma once

#include "ast.hpp"
#include "functions.hpp"
#include "numbers.hpp"
#include "uncertainty.hpp"
#include "units.hpp"

#include <calculate-core/calculate-core.hpp>

#include <algorithm>
#include <atomic>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace calculate_core::detail {

// Node values of one evaluation in T.
template <class T>
struct Forward {
    std::optional<Error> error;
    std::vector<T> values;
    std::vector<int> roundings;  // per node, for Counted functions
    std::vector<T> scales;       // per node, for kernels that subtract large terms (Applied::scale)
};

namespace impl {

inline Error nodeError(const Node& node, ErrorCode code, std::string message) {
    return Error{code, std::move(message), node.span.begin, node.span.end};
}

// A product that is zero whenever a factor is zero, even if the other is infinite
// (an infinite derivative times an exactly known input contributes nothing).
inline Ruler times(const Ruler& a, const Ruler& b) {
    return a == 0 || b == 0 ? Ruler(0) : Ruler(a * b);
}

// Whether an argument's error could carry x across a place where a one-argument piecewise function jumps.
// To first order the nearest jump is enough.
inline bool nearJumpOne(FunctionId id, const Rational& x, const Ruler& bound) {
    Rational jump;
    switch (id) {
    case FunctionId::Acot:
    case FunctionId::Sgn: jump = 0; break;
    case FunctionId::Floor: jump = Rational(floorOf(x + Rational(1, 2))); break;  // the nearest integer
    case FunctionId::Round: jump = Rational(floorOf(x)) + Rational(1, 2); break;  // the nearest half-integer
    case FunctionId::Trunc:  // the nearest integer, except 0, where trunc is continuous
        jump = Rational(floorOf(x + Rational(1, 2)));
        if (jump == 0) return false;
        break;
    default: return false;
    }
    return fromRational<Ruler>(abs(x - jump)) <= bound;
}

// The Ruler rounds to nearest, so a bound whose parts are all exact (and so equal to the true error) can come out a
// few ulps low: a point exactly at the end of the error interval must still count as reached. 2^-900 is far above
// those ulps and far below anything a bound means (the tests allow the same).
inline Ruler widened(const Ruler& bound) {
    using std::ldexp;
    return bound * (Ruler(1) + ldexp(Ruler(1), -900));
}

// Whether the arguments' errors could carry x/y across a whole number k where a remainder jumps: every k for a
// floored one, k != 0 for a truncated one (continuous at 0). To first order the jump at k is |x - k*y| away, and
// the errors move x - k*y by at most bx + |k|*by. The nearest k and its two neighbours are enough.
inline bool nearJump(const Rational& x, const Rational& y, const Ruler& bx, const Ruler& by, bool jumpsAtZero) {
    const Integer n = floorOf(x / y + Rational(1, 2));  // y != 0: the forward pass succeeded
    for (const Integer& k : {Integer(n - 1), n, Integer(n + 1)}) {
        if (k == 0 && !jumpsAtZero) continue;
        const Ruler distance = fromRational<Ruler>(abs(x - Rational(k) * y));
        if (distance <= widened(bx + fromRational<Ruler>(Rational(abs(k))) * by)) return true;
    }
    return false;
}

// Whether the error interval [value - bound, value + bound] reaches `point`. An exactly known value never does.
inline bool near(const Rational& value, const Rational& point, const Ruler& bound) {
    return bound > 0 && fromRational<Ruler>(abs(value - point)) <= bound;
}

// The argument whose error interval reaches a point where the node's function is not defined, not finite or
// not smooth, or -1. Values and bounds are the arguments'.
inline int edgeReached(FunctionId id, const std::vector<Rational>& x, const std::vector<Ruler>& b) {
    switch (id) {
    case FunctionId::Divide: return near(x[1], 0, b[1]) ? 1 : -1;
    case FunctionId::Sqrt:
    case FunctionId::Cbrt:
    case FunctionId::Ln:
    case FunctionId::Log10:
    case FunctionId::Csch: return near(x[0], 0, b[0]) ? 0 : -1;
    case FunctionId::Root: {
        const bool linear = b[1] == 0 && abs(x[1]) == 1;
        return !linear && near(x[0], 0, b[0]) ? 0 : -1;
    }
    case FunctionId::LogBase:
        if (near(x[0], 0, b[0])) return 0;
        return near(x[1], 0, b[1]) || near(x[1], 1, b[1]) ? 1 : -1;
    case FunctionId::Power: {
        const bool wholeExponent = b[1] == 0 && denominator(x[1]) == 1 && x[1] >= 0;
        return !wholeExponent && near(x[0], 0, b[0]) ? 0 : -1;
    }
    case FunctionId::Asin:
    case FunctionId::Acos:
    case FunctionId::Atanh: return near(x[0], -1, b[0]) || near(x[0], 1, b[0]) ? 0 : -1;
    case FunctionId::Acosh: return near(x[0], 1, b[0]) ? 0 : -1;
    case FunctionId::Tan: {  // the nearest pole (k + 1/2)pi
        const Rational pi = constantRational(ConstantId::Pi);
        const Integer k = floorOf(x[0] / pi);
        return near(x[0], (Rational(k) + Rational(1, 2)) * pi, b[0]) ? 0 : -1;
    }
    case FunctionId::Rem:
    case FunctionId::FloorMod: return near(x[1], 0, b[1]) ? 1 : -1;
    case FunctionId::Gamma:
    case FunctionId::Lgamma:
    case FunctionId::Digamma: {  // the poles: the nearest non-positive integer
        const Rational k = x[0] < Rational(1, 2) ? Rational(floorOf(x[0] + Rational(1, 2))) : Rational(0);
        return near(x[0], k, b[0]) ? 0 : -1;
    }
    case FunctionId::Beta: return near(x[0], 0, b[0]) ? 0 : near(x[1], 0, b[1]) ? 1 : -1;
    case FunctionId::Erfinv: return near(x[0], 1, b[0]) || near(x[0], -1, b[0]) ? 0 : -1;
    case FunctionId::Erfcinv: return near(x[0], 0, b[0]) || near(x[0], 2, b[0]) ? 0 : -1;
    case FunctionId::GammaP:
    case FunctionId::GammaQ:
    case FunctionId::Igamma:
    case FunctionId::GammaInc:  // a at 0; x at 0, the end of its domain
        if (near(x[0], 0, b[0])) return 0;
        return near(x[1], 0, b[1]) ? 1 : -1;
    case FunctionId::Betainc:  // a, b at 0; x at 0 and 1, the ends of its domain
        if (near(x[0], 0, b[0])) return 0;
        if (near(x[1], 0, b[1])) return 1;
        return near(x[2], 0, b[2]) || near(x[2], 1, b[2]) ? 2 : -1;
    case FunctionId::Betaincinv:  // a, b at 0; y at 0 and 1
        if (near(x[0], 0, b[0])) return 0;
        if (near(x[1], 0, b[1])) return 1;
        return near(x[2], 0, b[2]) || near(x[2], 1, b[2]) ? 2 : -1;
    case FunctionId::Atan2: {  // the origin, where the angle is undefined: both coordinates 0 within their errors
        const bool y0 = x[0] == 0 || near(x[0], 0, b[0]);
        const bool x0 = x[1] == 0 || near(x[1], 0, b[1]);
        return y0 && x0 && (b[0] > 0 || b[1] > 0) ? (b[0] > 0 ? 0 : 1) : -1;
    }
    default: return -1;
    }
}

// 0^y jumps at y = 0 (0^0 = 1, 0^y = 0 for y > 0).
inline bool zeroPowerNearJump(const std::vector<Rational>& x, const std::vector<Ruler>& b) {
    return x[0] == 0 && b[0] == 0 && near(x[1], 0, b[1]);
}

}  // namespace impl

// Each node's own error: input error for literals and constants, rounding or library error otherwise.
template <class T>
std::vector<Ruler> localErrors(const Ast& ast, const Forward<T>& fw) {
    using std::abs;
    std::vector<Ruler> locals(ast.nodes.size(), Ruler(0));
    for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
        const Node& node = ast.nodes[i];
        if (node.function == FunctionId::Literal) {
            locals[i] = fromRational<Ruler>(abs(toRational(*parseDecimal(node.text)) - toRational(fw.values[i])));
        } else if (const auto c = tableConstant(node.function)) {
            locals[i] = fromRational<Ruler>(abs(constantRational(*c) - toRational(fw.values[i])));
        } else {
            std::vector<T> args;
            for (const int a : node.args) args.push_back(fw.values[a]);
            Applied<T> applied;
            applied.value = fw.values[i];
            applied.roundings = fw.roundings[i];
            applied.scale = fw.scales[i];
            locals[i] = localError<T>(node.function, args, applied);
        }
    }
    return locals;
}

// d(node)/d(argument k), evaluated at the computed values, for every node.
template <class T>
std::vector<std::vector<Ruler>> nodePartials(const Ast& ast, const Forward<T>& fw) {
    std::vector<std::vector<Ruler>> result(ast.nodes.size());
    for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
        const Node& node = ast.nodes[i];
        if (node.args.empty()) continue;
        std::vector<Ruler> args;
        for (const int a : node.args) args.push_back(exactCast<Ruler>(fw.values[a]));
        result[i] = partials<Ruler>(node.function, args, exactCast<Ruler>(fw.values[i]));
    }
    return result;
}

struct Adjoints {
    std::vector<Ruler> signedAdj;    // d(root)/d(node): sums over all paths
    std::vector<Ruler> absoluteAdj;  // the same with |partials|: used for guaranteed bounds
};

// Reverse sweep: parents come after their arguments, so walking backwards visits each node's
// parents before the node itself. The signed adjoints use the derivatives, the absolute ones the slopes.
inline Adjoints adjoints(const Ast& ast, const std::vector<std::vector<Ruler>>& partials,
                         const std::vector<std::vector<Ruler>>& slopes) {
    Adjoints adj;
    adj.signedAdj.assign(ast.nodes.size(), Ruler(0));
    adj.absoluteAdj.assign(ast.nodes.size(), Ruler(0));
    adj.signedAdj.back() = 1;
    adj.absoluteAdj.back() = 1;
    for (int i = ast.root(); i >= 0; --i)
        for (std::size_t k = 0; k < ast.nodes[i].args.size(); ++k) {
            const int a = ast.nodes[i].args[k];
            adj.signedAdj[a] += impl::times(partials[i][k], adj.signedAdj[i]);
            adj.absoluteAdj[a] += impl::times(slopes[i][k], adj.absoluteAdj[i]);
        }
    return adj;
}

// The same with |derivatives| as the slopes: first order at the computed values.
inline Adjoints adjoints(const Ast& ast, const std::vector<std::vector<Ruler>>& partials) {
    using std::abs;
    std::vector<std::vector<Ruler>> magnitudes = partials;
    for (std::vector<Ruler>& node : magnitudes)
        for (Ruler& d : node) d = abs(d);
    return adjoints(ast, partials, magnitudes);
}

// Evaluates every node in T. The first error stops the pass and carries the failing node's span.
template <class T>
// `shifts` (when given) moves nodes' values by that much as they are computed (an uncertain quantity at a corner).
Forward<T> forward(const Ast& ast, const std::atomic<bool>* cancel = nullptr, const std::vector<Rational>* shifts = nullptr) {
    Forward<T> fw;
    fw.values.resize(ast.nodes.size());
    fw.roundings.assign(ast.nodes.size(), 0);
    fw.scales.assign(ast.nodes.size(), T(0));
    for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
        const Node& node = ast.nodes[i];
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            fw.error = impl::nodeError(node, ErrorCode::Cancelled, errorMessage(ErrorCode::Cancelled, ""));
            return fw;
        }
        if (node.function == FunctionId::Literal) {
            // An exact literal beyond 10^±1000000 cannot be materialized in reasonable time or memory.
            const auto literal = parseDecimal(node.text);
            const bool outOfRange = !literal || (isExact<T> && (literal->exponent10 > exactDigitsLimit
                                                                || literal->exponent10 < -exactDigitsLimit));
            if (!outOfRange) fw.values[i] = decimalTo<T>(*literal);
            if (outOfRange || !isFinite(fw.values[i])) {
                fw.error = impl::nodeError(node, ErrorCode::LiteralOutOfRange,
                                           errorMessage(ErrorCode::LiteralOutOfRange, ""));
                return fw;
            }
            continue;
        }
        if (node.function == FunctionId::ErrorPart) {  // its argument's worst-case uncertainty, as a number
            const std::size_t a = static_cast<std::size_t>(node.args[0]);
            Ast sub;
            sub.nodes.assign(ast.nodes.begin(), ast.nodes.begin() + static_cast<std::ptrdiff_t>(a + 1));
            Forward<T> prefix;
            prefix.values.assign(fw.values.begin(), fw.values.begin() + static_cast<std::ptrdiff_t>(a + 1));
            prefix.roundings.assign(fw.roundings.begin(), fw.roundings.begin() + static_cast<std::ptrdiff_t>(a + 1));
            prefix.scales.assign(fw.scales.begin(), fw.scales.begin() + static_cast<std::ptrdiff_t>(a + 1));
            const Adjoints adj = adjoints(sub, nodePartials<T>(sub, prefix));
            fw.values[i] = fromRational<T>(toRational(combine(sub, adj.signedAdj, userUncertainties(sub, prefix.values)).linear));
            continue;
        }
        std::vector<T> args;
        for (const int a : node.args) args.push_back(fw.values[a]);
        const Applied<T> r = applyFunction<T>(node.function, args, cancel);
        if (r.error) {
            // A lowering's division by zero is a point where the written function is not defined.
            const ErrorCode code = node.lowered && *r.error == ErrorCode::DivisionByZero ? ErrorCode::DomainError : *r.error;
            fw.error = impl::nodeError(node, code, errorMessage(code, nameOf(node)));
            return fw;
        }
        fw.values[i] = r.value;
        if (shifts && (*shifts)[i] != 0) fw.values[i] = fromRational<T>(toRational(r.value) + (*shifts)[i]);
        fw.roundings[i] = r.roundings;
        fw.scales[i] = r.scale;
    }
    return fw;
}


// First-order error bound of every node's value, computed forwards (Higham's running error bound).
// The user's uncertainties (when given) add to their nodes' bounds.
inline std::vector<Ruler> forwardBounds(const Ast& ast, const std::vector<std::vector<Ruler>>& partials,
                                        const std::vector<Ruler>& locals, const std::vector<Ruler>& uncertainties = {}) {
    using std::abs;
    std::vector<Ruler> bounds(ast.nodes.size(), Ruler(0));
    for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
        bounds[i] = locals[i] + (uncertainties.empty() ? Ruler(0) : uncertainties[i]);
        for (std::size_t k = 0; k < ast.nodes[i].args.size(); ++k)
            bounds[i] += impl::times(abs(partials[i][k]), bounds[ast.nodes[i].args[k]]);
    }
    return bounds;
}

// Every node's error bound and the slopes it carries its arguments' bounds with (see `slopes`), computed forwards:
// a node's slopes need its arguments' bounds.
struct Propagation {
    std::vector<Ruler> bounds;
    std::vector<std::vector<Ruler>> slopes;
};

template <class T>
Propagation propagate(const Ast& ast, const Forward<T>& fw, const std::vector<Ruler>& locals,
                      const std::vector<Ruler>& uncertainties = {}) {
    Propagation p;
    p.bounds = locals;
    for (std::size_t i = 0; i < uncertainties.size(); ++i) p.bounds[i] += uncertainties[i];
    p.slopes.resize(ast.nodes.size());
    for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
        const Node& node = ast.nodes[i];
        if (node.args.empty()) continue;
        std::vector<Ruler> args;
        std::vector<Ruler> argBounds;
        for (const int a : node.args) {
            args.push_back(exactCast<Ruler>(fw.values[a]));
            argBounds.push_back(p.bounds[a]);
        }
        p.slopes[i] = slopes(node.function, args, argBounds);
        for (std::size_t k = 0; k < args.size(); ++k) p.bounds[i] += impl::times(p.slopes[i][k], argBounds[k]);
    }
    return p;
}

// Whether two reference evaluations agree within `tolerance`.
inline bool agree(const Rational& shadow, const Rational& check, const Rational& tolerance) {
    using std::abs;
    return abs(shadow - check) <= tolerance;
}

// "4.4e-17", "1e+16", "0", "inf": `significant` digits, trailing zeros removed. Computed in the
// ruler's arithmetic: an exact decimal expansion would be far too slow for tiny values.
inline std::string formatScientific(const Ruler& x, int significant = 2) {
    using std::abs;
    using std::floor;
    using std::frexp;
    if (x == 0) return "0";
    if (!isFinite(x)) return x < 0 ? "-inf" : "inf";
    const Ruler a = abs(x);
    int e2;
    frexp(a, &e2);
    long long e10 = (static_cast<long long>(e2) - 1) * 30103 / 100000;
    Ruler scaled = e10 >= 0 ? Ruler(a / powerOfTen(e10)) : Ruler(a * powerOfTen(-e10));
    for (; scaled >= 10; ++e10) scaled /= 10;
    for (; scaled < 1; --e10) scaled *= 10;
    Integer m(floor(scaled * powerOfTen(significant - 1) + Ruler(0.5)).convert_to<long long>());
    if (m == pow(Integer(10), static_cast<unsigned>(significant))) {
        m /= 10;
        ++e10;
    }
    std::string digits = m.str();
    digits.erase(digits.find_last_not_of('0') + 1);
    std::string s = x < 0 ? "-" : "";
    s += digits.substr(0, 1);
    if (digits.size() > 1) s += "." + digits.substr(1);
    s += e10 < 0 ? "e-" : "e+";
    s += std::to_string(e10 < 0 ? -e10 : e10);
    return s;
}

enum class Notation { Scientific, Engineering, Positional };

// The digits of d in a notation, the first `trusted` significant ones apart from the rest. Scientific: one digit
// before the point; engineering: one to three, the exponent a multiple of 3; positional: no exponent, with leading
// "0.000" or trailing zeros up to the units. A point goes with the digit after it, so neither part ends in one.
inline NumberParts formatParts(const DecimalDigits& d, int trusted, Notation notation) {
    NumberParts p;
    p.negative = d.negative;
    const long long n = static_cast<long long>(d.digits.size());
    const long long e = d.exponent10;
    long long point;  // the significant digits before the point
    if (notation == Notation::Positional) {
        p.hasExponent = false;
        if (e < 0) {
            p.trusted = "0." + std::string(static_cast<std::size_t>(-e - 1), '0');
            point = 0;
        } else {
            point = e + 1;
        }
    } else {
        const long long shift = notation == Notation::Engineering ? ((e % 3) + 3) % 3 : 0;
        point = shift + 1;
        p.hasExponent = true;
        p.exponent10 = e - shift;
    }
    const long long count = std::max(n, point);  // zeros pad the digits up to the point
    for (long long i = 0; i < count; ++i) {
        std::string& part = i < trusted ? p.trusted : p.noise;
        if (i == point && i > 0) part += '.';
        part += i < n ? d.digits[static_cast<std::size_t>(i)] : '0';
    }
    return p;
}

// Leading significant digits guaranteed by `error`: floor(-log10(error / |value|)), capped.
inline int trustedDigits(const Ruler& absValue, const Ruler& error, int digitCount) {
    if (error == 0) return digitCount;
    if (absValue == 0 || !isFinite(error)) return 0;
    Ruler q = error / absValue;
    int t = 0;
    for (; t < digitCount && q * 10 <= 1; ++t) q *= 10;
    return t;
}

struct Report {
    Uncertainty uncertainty;  // the user's uncertain inputs: apart from the bound, which stays computational
    UnitState unit;                 // the result's unit
    std::vector<Warning> unitNotes;  // units that differ (spans in the source text)
    Ruler input = 0;
    Ruler rounding = 0;
    Ruler library = 0;
    Ruler bound = 0;     // guaranteed: input + rounding + library
    Ruler measured = 0;  // |value - reference|
    Ruler condition = 0;
    bool measuredAvailable = false;
    bool reliable = false;
    bool boundComplete = true;
    int roundingOperations = 0;
};

template <class T>
struct Evaluation {
    std::optional<Error> error;
    T value{};
    Report report;
};

// The value in T and its error report: local errors weighted by the adjoints, the shadow
// evaluations for the measured error, and the condition number.
// `text` (the source, when known) names the arguments in the notes about units.
template <class T>
Evaluation<T> evaluate(const Ast& ast, const Options& options = {}, std::string_view text = {}) {
    using std::abs;
    Evaluation<T> ev;
    const Forward<T> fw = forward<T>(ast, options.cancel);
    if (fw.error) {
        ev.error = fw.error;
        return ev;
    }
    ev.value = fw.values.back();
    const std::vector<Ruler> locals = localErrors<T>(ast, fw);
    const std::vector<std::vector<Ruler>> parts = nodePartials<T>(ast, fw);
    Report& r = ev.report;

    // Discrete functions jump, so derivatives cannot carry their arguments' uncertainty:
    // refuse arguments that are not exactly known, unless the caller accepts an incomplete bound.
    const std::vector<Ruler> uncertainties = userUncertainties(ast, fw.values);
    const Propagation propagation = propagate<T>(ast, fw, locals, uncertainties);
    const std::vector<Ruler>& bounds = propagation.bounds;
    for (const Node& node : ast.nodes) {
        if (!node.args.empty()) {
            std::vector<Rational> x;
            std::vector<Ruler> b;
            for (const int a : node.args) {
                x.push_back(toRational(fw.values[a]));
                b.push_back(bounds[a]);
            }
            const int edge = impl::edgeReached(node.function, x, b);
            const bool jump = node.function == FunctionId::Power && impl::zeroPowerNearJump(x, b);
            if (edge >= 0 || jump) {
                if (!options.allowUncertainDiscreteArguments) {
                    const ErrorCode code = edge >= 0 ? ErrorCode::ArgumentNearEdge : ErrorCode::ArgumentNearJump;
                    const std::string carrier = edge >= 0 ? "argument" : "exponent";
                    ev.error = impl::nodeError(node, code,
                                               errorMessage(code, nameOf(node)) + "; its " + carrier
                                                   + " carries an error of up to " + formatScientific(b[edge >= 0 ? edge : 1]));
                    return ev;
                }
                r.boundComplete = false;
            }
            // A negative base exists only at whole exponents (odd orders for root): no slope carries their error.
            const bool power = node.function == FunctionId::Power;
            if ((power || node.function == FunctionId::Root) && x[0] < 0 && b[1] > 0) {
                if (!options.allowUncertainDiscreteArguments) {
                    const std::string what = power ? "exponent" : "order";
                    const std::string negative = power ? "base" : "radicand";
                    ev.error = impl::nodeError(node, ErrorCode::UncertainDiscreteArgument,
                                               nameOf(node) + " needs an exactly known " + what
                                                   + " when its " + negative + " is negative; its " + what
                                                   + " carries an error of up to " + formatScientific(b[1]));
                    return ev;
                }
                r.boundComplete = false;
            }
        }
        const FunctionInfo& info = functionInfo(node.function);
        if (info.continuity == Continuity::Continuous) continue;
        const std::string name = nameOf(node);
        if (info.continuity == Continuity::Piecewise && node.args.size() == 1) {
            const Ruler& bx = bounds[node.args[0]];
            if (bx == 0 || !impl::nearJumpOne(node.function, toRational(fw.values[node.args[0]]), bx)) continue;
            if (!options.allowUncertainDiscreteArguments) {
                ev.error = impl::nodeError(node, ErrorCode::ArgumentNearJump,
                                           name + " jumps within the error of its argument; its argument carries an error of up to "
                                               + formatScientific(bx));
                return ev;
            }
            r.boundComplete = false;
            continue;
        }
        if (info.continuity == Continuity::Piecewise) {
            const Ruler& bx = bounds[node.args[0]];
            const Ruler& by = bounds[node.args[1]];
            const Rational x = toRational(fw.values[node.args[0]]);
            const Rational y = toRational(fw.values[node.args[1]]);
            // atan2(y, x) jumps from π to −π across the negative x axis; the remainders at whole quotients.
            const bool reached = node.function == FunctionId::Atan2
                                     ? fromRational<Ruler>(abs(x)) <= bx && fromRational<Ruler>(y) - by < 0  // (y, x)
                                     : impl::nearJump(x, y, bx, by, node.function == FunctionId::FloorMod);
            if ((bx == 0 && by == 0) || !reached) continue;
            if (!options.allowUncertainDiscreteArguments) {
                ev.error = impl::nodeError(node, ErrorCode::ArgumentNearJump,
                                           errorMessage(ErrorCode::ArgumentNearJump, name) + "; they carry errors of up to "
                                               + formatScientific(bx) + " and " + formatScientific(by));
                return ev;
            }
            r.boundComplete = false;
            continue;
        }
        for (const int a : node.args) {
            if (bounds[a] == 0) continue;
            if (!options.allowUncertainDiscreteArguments) {
                ev.error = impl::nodeError(node, ErrorCode::UncertainDiscreteArgument,
                                           errorMessage(ErrorCode::UncertainDiscreteArgument, name)
                                               + "; its argument carries an error of up to " + formatScientific(bounds[a]));
                return ev;
            }
            r.boundComplete = false;
        }
    }

    const Adjoints adj = adjoints(ast, parts, propagation.slopes);
    r.uncertainty = combine(ast, adj.signedAdj, uncertainties);
    Ruler weighted = 0;  // sum over the inputs of |d root / d input| * |input|
    for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
        const Ruler term = impl::times(adj.absoluteAdj[i], locals[i]);
        switch (functionInfo(ast.nodes[i].function).errorClass) {
        case ErrorClass::Input:
            r.input += term;
            weighted += abs(impl::times(adj.signedAdj[i], exactCast<Ruler>(fw.values[i])));
            break;
        case ErrorClass::Library: r.library += term; break;
        case ErrorClass::Exact: break;
        default:
            r.rounding += term;
            if (locals[i] != 0) ++r.roundingOperations;
        }
    }
    r.bound = r.input + r.rounding + r.library;

    // Units: followed through the tree; exponents and orders must be exactly known.
    const auto exactValue = [&](int i) -> std::optional<Rational> {
        if (bounds[static_cast<std::size_t>(i)] != 0) return std::nullopt;
        return toRational(fw.values[static_cast<std::size_t>(i)]);
    };
    r.unit = unitsOf(ast, exactValue, r.unitNotes, text).back();

    // Is first order good enough? Every quantity at its worst-case corner, both ways, in the ruler.
    Uncertainty& u = r.uncertainty;
    if (!u.sources.empty() && r.boundComplete && isFinite(u.linear)) {
        std::vector<Rational> plus(ast.nodes.size(), Rational(0));
        for (const UncertainSource& s : u.sources)
            for (const int n : s.nodes) plus[static_cast<std::size_t>(n)] = Rational(s.direction) * toRational(s.uncertainty);
        std::vector<Rational> minus = plus;
        for (Rational& shift : minus) shift = -shift;
        const Forward<Ruler> centre = forward<Ruler>(ast, options.cancel);
        const Forward<Ruler> up = forward<Ruler>(ast, options.cancel, &plus);
        const Forward<Ruler> down = forward<Ruler>(ast, options.cancel, &minus);
        u.checked = true;
        if (centre.error || up.error || down.error) {
            u.reliable = false;
            u.observed = std::numeric_limits<Ruler>::infinity();
        } else {
            using std::max;
            u.observed = max(abs(up.values.back() - centre.values.back()), abs(down.values.back() - centre.values.back()));
            u.reliable = u.observed * 10 <= u.linear * 11;
        }
    }

    Rational reference = toRational(ev.value);
    if constexpr (isExact<T>) {
        r.measuredAvailable = true;
        r.reliable = true;
    } else {
        const Forward<Ruler> shadow = forward<Ruler>(ast, options.cancel);
        const Forward<RulerCheck> check = forward<RulerCheck>(ast, options.cancel);
        if (!shadow.error && !check.error) {
            reference = toRational(check.values.back());
            const Rational value = toRational(ev.value);
            const Rational measured = abs(value - reference);
            r.measured = fromRational<Ruler>(measured);
            r.measuredAvailable = true;
            // Reliable when the two shadows agree far below both the measured error and T's
            // resolution at the value (a tolerance relative to the reference fails when it is 0).
            const Rational floor = toRational((std::numeric_limits<T>::min)());
            const Rational resolution = toRational(unitRoundoff<T>()) * (abs(value) > floor ? abs(value) : floor);
            const Rational tolerance = scaleByPowerOfTwo(measured > resolution ? measured : resolution, -8);
            r.reliable = agree(toRational(shadow.values.back()), reference, tolerance);
        }
    }
    r.condition = reference == 0 ? std::numeric_limits<Ruler>::infinity()
                                 : Ruler(weighted / abs(fromRational<Ruler>(reference)));
    return ev;
}

}  // namespace calculate_core::detail
