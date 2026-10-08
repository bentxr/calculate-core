#pragma once

#include "numbers.hpp"

#include <array>
#include <string>

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

}  // namespace calculate_core::detail
