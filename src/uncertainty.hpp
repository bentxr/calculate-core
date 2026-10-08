#pragma once

#include "ast.hpp"
#include "numbers.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <string>
#include <vector>

// The user's uncertainty: values written with one (5 ± 0.2), how they reach the result.
namespace calculate_core::detail {

// Each node's own uncertainty u: |its uncertainty argument| for Uncertain nodes, 0 elsewhere.
template <class T>
std::vector<Ruler> userUncertainties(const Ast& ast, const std::vector<T>& values) {
    using std::abs;
    std::vector<Ruler> u(ast.nodes.size(), Ruler(0));
    for (std::size_t i = 0; i < ast.nodes.size(); ++i)
        if (ast.nodes[i].function == FunctionId::Uncertain) u[i] = abs(exactCast<Ruler>(values[ast.nodes[i].args[1]]));
    return u;
}

// One uncertain quantity: a value typed with ±, a measured constant, a number read with its precision.
struct UncertainSource {
    std::vector<int> nodes;  // its Uncertain nodes, in tree order (the same key)
    int node = 0;            // nodes.front(): its span names it
    Ruler uncertainty = 0;   // u, of its first node
    Ruler sensitivity = 0;   // |sum of the signed adjoints of its nodes|: |d result / d quantity|
    int direction = 1;       // the sign of that sum (+1 when it is 0): where the worst case lies
    Ruler contribution = 0;  // sensitivity * u (0 when u is 0, even with an infinite sensitivity)
};

struct Uncertainty {
    std::vector<UncertainSource> sources;  // largest contribution first; ties keep tree order
    Ruler linear = 0;                      // worst case: the sum of the contributions
    Ruler quadrature = 0;                  // statistical: the square root of the sum of their squares
    bool checked = false;                  // the corners were evaluated (there are uncertain inputs)
    bool reliable = true;                  // what the corners show stays within 1.1 times the worst case
    Ruler observed = 0;  // the largest change seen at the corners; +infinity when a corner could not be evaluated
};

// The nodes the result depends on, without going into an Uncertain node's uncertainty argument or an errorPart's
// argument (errorPart's value is a number: its argument's quantities do not reach the result).
inline std::vector<bool> reachable(const Ast& ast) {
    std::vector<bool> on(ast.nodes.size(), false);
    if (ast.nodes.empty()) return on;
    on[static_cast<std::size_t>(ast.root())] = true;
    for (std::size_t i = ast.nodes.size(); i-- > 0;) {
        if (!on[i]) continue;
        const Node& node = ast.nodes[i];
        for (std::size_t k = 0; k < node.args.size(); ++k)
            if (!(node.function == FunctionId::Uncertain && k == 1) && node.function != FunctionId::ErrorPart)
                on[static_cast<std::size_t>(node.args[k])] = true;
    }
    return on;
}

// The quantities (one per key: a constant's name, or each typed ± on its own), how much the result moves with each,
// and the two combinations.
inline Uncertainty combine(const Ast& ast, const std::vector<Ruler>& signedAdjoints, const std::vector<Ruler>& uncertainties) {
    using std::abs;
    using std::sqrt;
    Uncertainty u;
    const std::vector<bool> on = reachable(ast);
    std::map<std::string, std::size_t> position;
    for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
        const Node& node = ast.nodes[i];
        if (!on[i] || node.function != FunctionId::Uncertain) continue;
        const std::string key = node.text.empty() ? "#" + std::to_string(i) : node.text;
        const auto [at, added] = position.emplace(key, u.sources.size());
        if (added) u.sources.emplace_back();
        u.sources[at->second].nodes.push_back(static_cast<int>(i));
    }
    Ruler squares = 0;
    for (UncertainSource& s : u.sources) {
        s.node = s.nodes.front();
        s.uncertainty = uncertainties[static_cast<std::size_t>(s.node)];
        Ruler slope = 0;
        for (const int n : s.nodes) slope += signedAdjoints[static_cast<std::size_t>(n)];
        if (slope != slope) slope = std::numeric_limits<Ruler>::infinity();  // two infinite slopes of opposite sign
        s.direction = slope < 0 ? -1 : 1;
        s.sensitivity = abs(slope);
        s.contribution = s.uncertainty == 0 ? Ruler(0) : Ruler(s.sensitivity * s.uncertainty);
        u.linear += s.contribution;
        squares += s.contribution * s.contribution;
    }
    u.quadrature = sqrt(squares);
    std::stable_sort(u.sources.begin(), u.sources.end(),
                     [](const UncertainSource& a, const UncertainSource& b) { return a.contribution > b.contribution; });
    return u;
}

// A result with its total uncertainty (bound + the leading combination), as people write it.
struct UncertainForms {
    DecimalDigits shown;    // the uncertainty with two significant digits: {false, "20", -1} is 0.20
    std::string concise;    // "6.67430(15)e-11"; empty when total is 0 or not finite
    std::string plusMinus;  // "(6.67430 ± 0.00015)e-11"
};

namespace impl {

// The integer's digits with k of them after a decimal point, and at least one before it: ("5", 1) → "0.5".
inline std::string point(std::string digits, long long k) {
    if (k <= 0) return digits;
    if (static_cast<long long>(digits.size()) < k + 1) digits.insert(0, static_cast<std::size_t>(k + 1) - digits.size(), '0');
    digits.insert(digits.size() - static_cast<std::size_t>(k), ".");
    return digits;
}

}  // namespace impl

// The uncertainty to two significant digits (to nearest), and the value rounded half to even at its last digit.
inline UncertainForms uncertainForms(const Rational& value, const Ruler& total) {
    using std::abs;
    using std::floor;
    using std::frexp;
    UncertainForms f;
    if (total == 0 || !isFinite(total)) return f;
    int e2;
    frexp(total, &e2);
    long long e10 = (static_cast<long long>(e2) - 1) * 30103 / 100000;
    Ruler scaled = e10 >= 0 ? Ruler(total / powerOfTen(e10)) : Ruler(total * powerOfTen(-e10));
    for (; scaled >= 10; ++e10) scaled /= 10;
    for (; scaled < 1; --e10) scaled *= 10;
    long long q = e10 - 1;  // the place of the uncertainty's second digit
    long long m = floor(scaled * 10 + Ruler(0.5)).convert_to<long long>();
    if (m == 100) {
        m = 10;
        ++q;
    }
    f.shown = {false, std::to_string(m), q + 1};
    // |value| / 10^q to the nearest integer, ties to even.
    const Rational ten = q < 0 ? Rational(pow(Integer(10), static_cast<unsigned>(-q))) : Rational(1, pow(Integer(10), static_cast<unsigned>(q)));
    const Rational x = abs(value) * ten;
    const Integer num = numerator(x), den = denominator(x);
    Integer n = num / den;
    const Integer twice = 2 * (num % den);
    if (twice > den || (twice == den && n % 2 != 0)) ++n;
    const std::string digits = n.str();
    long long e = n == 0 ? q + 1 : static_cast<long long>(digits.size()) - 1 + q;  // the place of its first digit
    const std::string sign = value < 0 && n != 0 ? "-" : "";
    const std::string u = std::to_string(m);
    if (q <= 0 && -7 <= e && e < 21) {
        const std::string d = impl::point(digits, -q);
        f.concise = sign + d + "(" + u + ")";
        f.plusMinus = sign + d + " ± " + impl::point(u, -q);
        return f;
    }
    e = std::max(e, q + 1);
    const std::string mantissa = impl::point(digits, e - q);
    const std::string es = (e < 0 ? "e-" : "e+") + std::to_string(e < 0 ? -e : e);
    f.concise = sign + mantissa + "(" + u + ")" + es;
    f.plusMinus = "(" + sign + mantissa + " ± " + impl::point(u, e - q) + ")" + es;
    return f;
}

}  // namespace calculate_core::detail
