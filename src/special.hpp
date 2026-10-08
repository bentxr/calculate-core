#pragma once

#include "kernels.hpp"

#include <utility>
#include <vector>

namespace calculate_core::detail {

namespace impl {

template <class T>
int targetBits() { return 2 * precisionBits<T>() + 4; }  // the rule of negligible()

// Stirling's series is used from here up, where its terms keep shrinking long past the cut-off.
template <class T>
T stirlingShift() { return T((targetBits<T>() + 3) / 4); }

// B_2k / (2k (2k - 1)), k = 1 .. targetBits<RulerCheck>()/6 + 4, exact (DLMF 5.11.1), from the tangent numbers
// (Brent and Harvey 2011). Built once.
inline const std::vector<Rational>& stirlingRationals() {
    static const std::vector<Rational> list = [] {
        const int n = targetBits<RulerCheck>() / 6 + 4;
        std::vector<Integer> t(static_cast<std::size_t>(n) + 1);
        t[1] = 1;
        for (int k = 2; k <= n; ++k) t[k] = Integer(k - 1) * t[k - 1];
        for (int k = 2; k <= n; ++k)
            for (int j = k; j <= n; ++j) t[j] = Integer(j - k) * t[j - 1] + Integer(j - k + 2) * t[j];
        std::vector<Rational> c;
        for (int k = 1; k <= n; ++k) {
            const Integer four = pow(Integer(4), static_cast<unsigned>(k));
            Rational b(Integer(2 * k) * t[k], four * (four - 1));  // |B_2k|
            if (k % 2 == 0) b = -b;                                // (-1)^(k-1)
            c.push_back(b / Rational(2 * k * (2 * k - 1)));
        }
        return c;
    }();
    return list;
}

// The first targetBits<T>()/6 + 4 coefficients as double words.
template <class T>
const std::vector<DoubleWord<T>>& stirlingWords() {
    static const std::vector<DoubleWord<T>> words = [] {
        std::vector<DoubleWord<T>> w;
        const std::vector<Rational>& c = stirlingRationals();
        for (std::size_t k = 0; k < static_cast<std::size_t>(targetBits<T>() / 6 + 4); ++k) {
            const T hi = fromRational<T>(c[k]);
            w.push_back({hi, fromRational<T>(c[k] - toRational(hi))});
        }
        return w;
    }();
    return words;
}

template <class T>
const DoubleWord<T>& halfLogTwoPi() {  // ln(2 pi) / 2
    static const DoubleWord<T> value = scale(logWord(scale(word<T>(ConstantId::Pi), 1)), -1);
    return value;
}

template <class T>
const DoubleWord<T>& sqrtPi() {
    static const DoubleWord<T> value = sqrt(word<T>(ConstantId::Pi));
    return value;
}

}  // namespace impl

// sin(pi x) and cos(pi x) as double words. x = n + r with r = x - n exact and |r| <= 1/2, so sin(pi x) is accurate
// relative to itself even next to an integer (what the reflection formulas need).
template <class T>
std::pair<DoubleWord<T>, DoubleWord<T>> sinCosPi(const T& x) {
    using std::abs;
    using std::floor;
    T n = floor(x);
    T r = x - n;  // in [0, 1): exact
    if (r > T(0.5)) {
        n = n + 1;
        r = r - 1;
    }
    const DoubleWord<T>& pi = impl::word<T>(ConstantId::Pi);
    const T ar = abs(r);
    DoubleWord<T> s, c;
    if (ar <= T(0.25)) {
        s = sinSmall(pi * ar);
        c = cosSmall(pi * ar);
    } else {
        const T q = T(0.5) - ar;  // exact
        s = cosSmall(pi * q);
        c = sinSmall(pi * q);
    }
    if (r < 0) s = -s;
    if (impl::isOdd(n)) {
        s = -s;
        c = -c;
    }
    return {s, c};
}

}  // namespace calculate_core::detail
