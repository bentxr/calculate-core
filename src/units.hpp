#pragma once

#include "ast.hpp"
#include "functions.hpp"
#include "numbers.hpp"
#include "physical_constants.hpp"

#include <calculate-core/calculate-core.hpp>

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The units of results: a quantity's dimension, as the exponents of the SI base units, and its name.
namespace calculate_core::detail {

// The exponents of the SI base units m, kg, s, A, K, mol, cd in a quantity's dimension.
struct Dimension {
    std::array<Rational, 7> exponents{};
    bool none() const {  // dimensionless
        for (const Rational& e : exponents)
            if (e != 0) return false;
        return true;
    }
};

inline bool operator==(const Dimension& a, const Dimension& b) { return a.exponents == b.exponents; }
inline bool operator!=(const Dimension& a, const Dimension& b) { return !(a == b); }

inline Dimension operator*(const Dimension& a, const Dimension& b) {
    Dimension d;
    for (std::size_t i = 0; i < d.exponents.size(); ++i) d.exponents[i] = a.exponents[i] + b.exponents[i];
    return d;
}

inline Dimension operator/(const Dimension& a, const Dimension& b) {
    Dimension d;
    for (std::size_t i = 0; i < d.exponents.size(); ++i) d.exponents[i] = a.exponents[i] - b.exponents[i];
    return d;
}

inline Dimension power(const Dimension& d, const Rational& exponent) {
    Dimension p;
    for (std::size_t i = 0; i < p.exponents.size(); ++i) p.exponents[i] = d.exponents[i] * exponent;
    return p;
}

namespace impl {

inline Dimension base(std::initializer_list<int> exponents) {
    Dimension d;
    std::size_t i = 0;
    for (const int e : exponents) d.exponents[i++] = e;
    return d;
}

// The named SI units, in the order unitName tries them.
inline const std::array<std::pair<const char*, Dimension>, 12>& namedUnits() {
    static const std::array<std::pair<const char*, Dimension>, 12> units{{
        {"J", base({2, 1, -2})}, {"W", base({2, 1, -3})}, {"C", base({0, 0, 1, 1})}, {"V", base({2, 1, -3, -1})},
        {"N", base({1, 1, -2})}, {"Pa", base({-1, 1, -2})}, {"F", base({-2, -1, 4, 2})}, {"\xCE\xA9", base({2, 1, -3, -2})},
        {"S", base({-2, -1, 3, 2})}, {"Wb", base({2, 1, -2, -1})}, {"T", base({0, 1, -2, -1})}, {"H", base({2, 1, -2, -2})},
    }};
    return units;
}

inline constexpr std::array<const char*, 7> baseSymbols{"m", "kg", "s", "A", "K", "mol", "cd"};

// A whole exponent in superscript digits: 2 → "²", −1 → "⁻¹".
inline std::string superscript(const Integer& n) {
    static const std::array<const char*, 10> digits{"\xE2\x81\xB0", "\xC2\xB9", "\xC2\xB2", "\xC2\xB3", "\xE2\x81\xB4",
                                                    "\xE2\x81\xB5", "\xE2\x81\xB6", "\xE2\x81\xB7", "\xE2\x81\xB8", "\xE2\x81\xB9"};
    std::string s = n < 0 ? "\xE2\x81\xBB" : "";
    for (const char c : Integer(n < 0 ? Integer(-n) : n).str()) s += digits[static_cast<std::size_t>(c - '0')];
    return s;
}

// The base units with their exponents: m²·s⁻², m^(1/2).
inline std::string baseForm(const Dimension& d) {
    std::string s;
    for (std::size_t i = 0; i < d.exponents.size(); ++i) {
        const Rational& e = d.exponents[i];
        if (e == 0) continue;
        if (!s.empty()) s += "\xC2\xB7";
        s += baseSymbols[i];
        if (e == 1) continue;
        if (denominator(e) == 1) s += superscript(numerator(e));
        else s += "^(" + numerator(e).str() + "/" + denominator(e).str() + ")";
    }
    return s;
}

}  // namespace impl

// The unit as people write it: a named SI unit (J W C V N Pa F Ω S Wb T H) when it matches; when the base form
// would need three base units or more, a named unit times one base unit to a whole power (J·s, J·K⁻¹); otherwise
// the base units with superscripts (m²·s⁻², m³·kg⁻¹·s⁻²).
inline std::string unitName(const Dimension& d) {
    for (const auto& [name, dimension] : impl::namedUnits())
        if (d == dimension) return name;
    int used = 0;
    for (const Rational& e : d.exponents) used += e != 0;
    if (used >= 3)
        for (const auto& [name, dimension] : impl::namedUnits()) {
            const Dimension rest = d / dimension;
            int left = 0;
            std::size_t at = 0;
            for (std::size_t i = 0; i < rest.exponents.size(); ++i)
                if (rest.exponents[i] != 0) {
                    ++left;
                    at = i;
                }
            if (left == 1 && denominator(rest.exponents[at]) == 1) return std::string(name) + "\xC2\xB7" + impl::baseForm(rest);
        }
    return impl::baseForm(d);
}

struct UnitState {
    enum class Kind { Known, Unknown, Mismatch } kind = Kind::Known;
    Dimension dimension;  // when Known
};

namespace impl {

inline Dimension constantDimension(const PhysicalConstant& c) {
    Dimension d;
    for (std::size_t i = 0; i < d.exponents.size(); ++i) d.exponents[i] = c.dimension[i];
    return d;
}

inline std::string unitOrNone(const Dimension& d) { return d.none() ? "none" : unitName(d); }

inline std::string_view sourceOf(std::string_view text, const Node& n) {
    return n.span.end <= text.size() ? text.substr(n.span.begin, n.span.end - n.span.begin) : std::string_view{};
}

}  // namespace impl

// Every node's unit: a constant's from the table; numbers none; operations by their rules. `exactValue(i)`
// is node i's value when it is exactly known (its forward bound is 0), for exponents.
inline std::vector<UnitState> unitsOf(const Ast& ast, const std::function<std::optional<Rational>(int)>& exactValue,
                                      std::vector<Warning>& notes, std::string_view text) {
    using Kind = UnitState::Kind;
    const std::size_t n = ast.nodes.size();
    std::vector<UnitState> u(n);
    // A constant's definition is not inspected: the nodes under a constant take no part.
    std::vector<bool> inside(n, false);
    for (std::size_t i = n; i-- > 0;)
        if (inside[i] || ast.nodes[i].constant >= 0)
            for (const int a : ast.nodes[i].args) inside[static_cast<std::size_t>(a)] = true;
    const auto note = [&](const Node& node, std::string message) {
        notes.push_back({WarningCode::UnitsDiffer, std::move(message), node.span.begin, node.span.end});
    };
    for (std::size_t i = 0; i < n; ++i) {
        if (inside[i]) continue;
        const Node& node = ast.nodes[i];
        UnitState& out = u[i];
        if (node.constant >= 0) {
            const PhysicalConstant& c = physicalConstants[static_cast<std::size_t>(node.constant)];
            if (c.coherent) out.dimension = impl::constantDimension(c);
            else out.kind = Kind::Unknown;
            continue;
        }
        if (node.args.empty()) continue;  // numbers, pi, e and the other mathematical constants: none
        const FunctionId f = node.function;
        // The arguments that carry a quantity (an Uncertain node's uncertainty is information).
        const std::size_t used = f == FunctionId::Uncertain ? 1 : node.args.size();
        bool mismatch = false, unknown = false;
        for (std::size_t k = 0; k < used; ++k) {
            const UnitState& a = u[static_cast<std::size_t>(node.args[k])];
            mismatch = mismatch || a.kind == Kind::Mismatch;
            unknown = unknown || a.kind == Kind::Unknown;
        }
        if (mismatch || unknown) {
            out.kind = mismatch ? Kind::Mismatch : Kind::Unknown;
            continue;
        }
        const auto arg = [&](std::size_t k) -> const Dimension& { return u[static_cast<std::size_t>(node.args[k])].dimension; };
        const auto argNode = [&](std::size_t k) -> const Node& { return ast.nodes[static_cast<std::size_t>(node.args[k])]; };
        switch (f) {
        case FunctionId::Add:
        case FunctionId::Subtract:
        case FunctionId::Rem:
        case FunctionId::FloorMod:
        case FunctionId::Hypot:
        case FunctionId::Clip:
        case FunctionId::Median:
        case FunctionId::Atan2: {  // every argument in the same unit
            for (std::size_t k = 1; k < used; ++k)
                if (arg(k) != arg(0)) {
                    out.kind = Kind::Mismatch;
                    note(node, "the units of " + std::string(impl::sourceOf(text, argNode(0))) + " (" + impl::unitOrNone(arg(0))
                                   + ") and " + std::string(impl::sourceOf(text, argNode(k))) + " (" + impl::unitOrNone(arg(k))
                                   + ") differ");
                    break;
                }
            if (out.kind == Kind::Known && f != FunctionId::Atan2) out.dimension = arg(0);
            break;
        }
        case FunctionId::Multiply: out.dimension = arg(0) * arg(1); break;
        case FunctionId::Divide: out.dimension = arg(0) / arg(1); break;
        case FunctionId::Negate:
        case FunctionId::Abs:
        case FunctionId::Floor:
        case FunctionId::Trunc:
        case FunctionId::Round:
        case FunctionId::Uncertain:
        case FunctionId::Percent:
        case FunctionId::PerMille:
        case FunctionId::PerMyriad: out.dimension = arg(0); break;
        case FunctionId::Square: out.dimension = power(arg(0), Rational(2)); break;
        case FunctionId::Cube: out.dimension = power(arg(0), Rational(3)); break;
        case FunctionId::Sqrt: out.dimension = power(arg(0), Rational(1, 2)); break;
        case FunctionId::Cbrt: out.dimension = power(arg(0), Rational(1, 3)); break;
        case FunctionId::Power:
        case FunctionId::Root: {
            if (!arg(1).none()) {
                out.kind = Kind::Mismatch;
                note(node, std::string(symbolOf(f)) + " needs a number without a unit; " + std::string(impl::sourceOf(text, argNode(1)))
                               + " has " + unitName(arg(1)));
                break;
            }
            if (arg(0).none()) break;  // a number to any power: none
            const std::optional<Rational> e = exactValue(node.args[1]);
            if (!e || (f == FunctionId::Root && *e == 0)) {
                out.kind = Kind::Unknown;
                break;
            }
            out.dimension = power(arg(0), f == FunctionId::Power ? *e : Rational(1) / *e);
            break;
        }
        default:  // sin, exp, ln, gamma, the discrete functions…: numbers without a unit
            for (std::size_t k = 0; k < used; ++k)
                if (!arg(k).none()) {
                    out.kind = Kind::Mismatch;
                    note(node, nameOf(node) + " needs a number without a unit; " + std::string(impl::sourceOf(text, argNode(k)))
                                   + " has " + unitName(arg(k)));
                    break;
                }
        }
    }
    return u;
}

}  // namespace calculate_core::detail
