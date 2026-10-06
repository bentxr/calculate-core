#pragma once

#include "ast.hpp"
#include "functions.hpp"
#include "numbers.hpp"

#include <calculate-core/calculate-core.hpp>

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

// Whether the arguments' errors could carry x/y across a whole number k != 0, where a truncated remainder
// jumps (it is continuous at 0). To first order the jump at k is |x - k*y| away, and the errors move
// x - k*y by at most bx + |k|*by. The nearest k and its two neighbours are enough.
inline bool nearJump(const Rational& x, const Rational& y, const Ruler& bx, const Ruler& by) {
    const Integer n = floorOf(x / y + Rational(1, 2));  // y != 0: the forward pass succeeded
    for (const Integer& k : {Integer(n - 1), n, Integer(n + 1)}) {
        if (k == 0) continue;
        const Ruler distance = fromRational<Ruler>(abs(x - Rational(k) * y));
        if (distance <= bx + fromRational<Ruler>(Rational(abs(k))) * by) return true;
    }
    return false;
}

}  // namespace impl

// Evaluates every node in T. The first error stops the pass and carries the failing node's span.
template <class T>
Forward<T> forward(const Ast& ast, const std::atomic<bool>* cancel = nullptr) {
    Forward<T> fw;
    fw.values.resize(ast.nodes.size());
    fw.roundings.assign(ast.nodes.size(), 0);
    for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
        const Node& node = ast.nodes[i];
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            fw.error = impl::nodeError(node, ErrorCode::Cancelled, errorMessage(ErrorCode::Cancelled, ""));
            return fw;
        }
        if (node.function == FunctionId::Literal) {
            // An exact literal beyond 10^±1000000 cannot be materialized in reasonable time or memory.
            const auto literal = parseDecimal(node.text);
            const bool outOfRange =
                !literal || (isExact<T> && (literal->exponent10 > 1000000 || literal->exponent10 < -1000000));
            if (!outOfRange) fw.values[i] = decimalTo<T>(*literal);
            if (outOfRange || !isFinite(fw.values[i])) {
                fw.error = impl::nodeError(node, ErrorCode::LiteralOutOfRange,
                                           errorMessage(ErrorCode::LiteralOutOfRange, ""));
                return fw;
            }
            continue;
        }
        std::vector<T> args;
        for (const int a : node.args) args.push_back(fw.values[a]);
        const Applied<T> r = applyFunction<T>(node.function, args, cancel);
        if (r.error) {
            fw.error = impl::nodeError(node, *r.error, errorMessage(*r.error, functionInfo(node.function).name));
            return fw;
        }
        fw.values[i] = r.value;
        fw.roundings[i] = r.roundings;
    }
    return fw;
}

// Each node's own error: input error for literals and constants, rounding or library error otherwise.
template <class T>
std::vector<Ruler> localErrors(const Ast& ast, const Forward<T>& fw) {
    using std::abs;
    std::vector<Ruler> locals(ast.nodes.size(), Ruler(0));
    for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
        const Node& node = ast.nodes[i];
        if (node.function == FunctionId::Literal) {
            locals[i] = fromRational<Ruler>(abs(toRational(*parseDecimal(node.text)) - toRational(fw.values[i])));
        } else if (node.function == FunctionId::Pi || node.function == FunctionId::E) {
            const ConstantId c = node.function == FunctionId::Pi ? ConstantId::Pi : ConstantId::E;
            locals[i] = fromRational<Ruler>(abs(constantRational(c) - toRational(fw.values[i])));
        } else {
            std::vector<T> args;
            for (const int a : node.args) args.push_back(fw.values[a]);
            Applied<T> applied;
            applied.value = fw.values[i];
            applied.roundings = fw.roundings[i];
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

// First-order error bound of every node's value, computed forwards (Higham's running error bound).
inline std::vector<Ruler> forwardBounds(const Ast& ast, const std::vector<std::vector<Ruler>>& partials,
                                        const std::vector<Ruler>& locals) {
    using std::abs;
    std::vector<Ruler> bounds(ast.nodes.size(), Ruler(0));
    for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
        bounds[i] = locals[i];
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
Propagation propagate(const Ast& ast, const Forward<T>& fw, const std::vector<Ruler>& locals) {
    Propagation p;
    p.bounds = locals;
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
    const auto power10 = [](long long n) {
        Ruler result = 1;
        Ruler base = 10;
        for (unsigned long long k = static_cast<unsigned long long>(n < 0 ? -n : n); k; k >>= 1) {
            if (k & 1) result *= base;
            base *= base;
        }
        return result;
    };
    const Ruler a = abs(x);
    int e2;
    frexp(a, &e2);
    long long e10 = (static_cast<long long>(e2) - 1) * 30103 / 100000;
    Ruler scaled = e10 >= 0 ? Ruler(a / power10(e10)) : Ruler(a * power10(-e10));
    for (; scaled >= 10; ++e10) scaled /= 10;
    for (; scaled < 1; --e10) scaled *= 10;
    Integer m(floor(scaled * power10(significant - 1) + Ruler(0.5)).convert_to<long long>());
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
template <class T>
Evaluation<T> evaluate(const Ast& ast, const Options& options = {}) {
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
    const Propagation propagation = propagate<T>(ast, fw, locals);
    const std::vector<Ruler>& bounds = propagation.bounds;
    for (const Node& node : ast.nodes) {
        const FunctionInfo& info = functionInfo(node.function);
        if (info.continuity == Continuity::Continuous) continue;
        const std::string name = info.name.empty() ? "!" : std::string(info.name);
        if (info.continuity == Continuity::Piecewise) {
            const Ruler& bx = bounds[node.args[0]];
            const Ruler& by = bounds[node.args[1]];
            const Rational x = toRational(fw.values[node.args[0]]);
            const Rational y = toRational(fw.values[node.args[1]]);
            if ((bx == 0 && by == 0) || !impl::nearJump(x, y, bx, by)) continue;
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
