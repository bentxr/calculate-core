#pragma once

#include "kernels.hpp"

#include <calculate-core/calculate-core.hpp>

#include <atomic>
#include <optional>
#include <utility>
#include <vector>

namespace calculate_core::detail {

namespace impl {

template <class T>
int targetBits() { return 2 * precisionBits<T>() + 4; }  // the rule of negligible()

constexpr int maxIterations = 1 << 20;  // beyond this a series or fraction gives up (ArgumentTooLarge)

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

template <class T>
struct Special {
    DoubleWord<T> value;
    T scale = 0;  // see Applied::scale
    std::optional<ErrorCode> error;
};

// ln Gamma(z) for z > 0 (a double word): shift z up to X by Gamma(z) = Gamma(z + n) / (z (z+1) ... (z+n-1)), then
// Stirling's series (DLMF 5.11.1). The result is S - ln P, so scale = |S| + |ln P| + n.
template <class T>
Special<T> lgammaPositive(DoubleWord<T> z) {
    using std::abs;
    using std::frexp;
    const T shift = impl::stirlingShift<T>();
    DoubleWord<T> product = dw(T(1));  // kept in [1/2, 1), its binary exponent in `exponent`: it never overflows
    long long exponent = 0;
    int n = 0;
    while (z.hi < shift) {
        product = product * z;
        int e;
        frexp(product.hi, &e);
        product = scale(product, -e);
        exponent += e;
        z = z + T(1);
        ++n;
    }
    DoubleWord<T> s = (z - T(0.5)) * logWord(z) - z + impl::halfLogTwoPi<T>();
    const DoubleWord<T> z2 = z * z;
    DoubleWord<T> power = z;
    DoubleWord<T> series = dw(T(0));
    for (const DoubleWord<T>& c : impl::stirlingWords<T>()) {
        const DoubleWord<T> term = c / power;
        if (impl::negligible(term, s)) break;
        series = series + term;
        power = power * z2;
    }
    s = s + series;
    Special<T> r;
    if (n == 0) {
        r.value = s;
        r.scale = abs(s.hi);
        return r;
    }
    const DoubleWord<T> lnP = logWord(product) + impl::word<T>(ConstantId::Ln2) * T(exponent);
    r.value = s - lnP;
    r.scale = abs(s.hi) + abs(lnP.hi) + T(n);
    return r;
}

template <class T>
std::pair<DoubleWord<T>, DoubleWord<T>> sinCosPi(const T& x);

// x² = p ln2 / 2: erfc(x) is about 2^(-p/2) there (the literal is a reduction boundary, not a precision).
template <class T>
T erfSwitch() {
    using std::sqrt;
    return sqrt(T(precisionBits<T>()) * T(0.6931471805599453) / 2);
}

// erf x for 0 <= x < erfSwitch (DLMF 7.6.2): (2/sqrt(pi)) e^-x² sum 2^k x^(2k+1) / (1·3·…·(2k+1)); all terms positive.
template <class T>
DoubleWord<T> erfSeries(const T& x) {
    const DoubleWord<T> x2 = dw(x) * x;
    const DoubleWord<T> twoX2 = scale(x2, 1);
    DoubleWord<T> term = dw(x), sum = dw(x);
    for (int k = 1;; ++k) {
        term = term * twoX2 / T(2 * k + 1);
        if (impl::negligible(term, sum)) break;
        sum = sum + term;
    }
    const ExpParts<T> e = expParts(-x2);
    const DoubleWord<T> ex = e.underflow ? dw(T(1)) : expValue(e);
    return scale(ex * sum / impl::sqrtPi<T>(), 1);
}

template <class T>
DoubleWord<T> erfcFraction(const T& x);

// x >= 0 with erf(x) = y (tail = false, 0 <= y <= 1/2) or erfc(x) = target (tail = true, 0 < target <= 1/2), by
// Newton; the last step, within 2 ulps, is taken in double words from the exact T point.
template <class T>
Special<T> erfInverse(const T& y, const T& target, bool tail) {
    using std::abs;
    using std::ldexp;
    using std::sqrt;
    T x = tail ? T(T(0.9) * sqrt(toValue(-logWord(dw(target) * (T(2) - target)))))  // ~ sqrt(-ln(1 - y²)): a start
               : toValue(impl::sqrtPi<T>() * y / T(2));                             // erf x ~ 2x / sqrt(pi)
    Special<T> r;
    for (int i = 0; i < 64; ++i) {
        const bool small = x < erfSwitch<T>();
        const DoubleWord<T> f = tail ? (small ? dw(T(1)) - erfSeries(x) : erfcFraction(x)) - target
                                     : (small ? erfSeries(x) : dw(T(1)) - erfcFraction(x)) - y;
        const DoubleWord<T> slope = scale(expValue(expParts(-(dw(x) * x))) / impl::sqrtPi<T>(), 1);
        DoubleWord<T> step = f / slope;
        if (tail) step = -step;  // erfc decreases
        const DoubleWord<T> next = dw(x) - step;
        if (abs(step.hi) <= ldexp(abs(x), 1 - precisionBits<T>())) {
            r.value = next;
            return r;
        }
        x = toValue(next);
    }
    r.value = dw(x);
    return r;
}

// erfcinv(z) for 0 < z <= 1: below 1/2 the target is z itself, so a tiny z keeps every bit (1 - z is exact above).
template <class T>
Special<T> erfcinvWord(const T& z) {
    return z < T(0.5) ? erfInverse(T(T(1) - z), z, true) : erfInverse(T(T(1) - z), T(0), false);
}

// erfc x for x >= erfSwitch, the continued fraction DLMF 7.9.2 by Lentz:
// e^-x² / sqrt(pi) / (x + (1/2)/(x + 1/(x + (3/2)/(x + …)))).
template <class T>
DoubleWord<T> erfcFraction(const T& x) {
    using std::abs;
    using std::ldexp;
    const DoubleWord<T> X = dw(x);
    DoubleWord<T> f = X, C = X, D = dw(T(0));
    for (int n = 1; n < (1 << 20); ++n) {
        const T a = T(n) / 2;
        D = dw(T(1)) / (X + D * a);
        C = X + dw(a) / C;
        const DoubleWord<T> delta = C * D;
        f = f * delta;
        if (abs((delta - T(1)).hi) <= ldexp(T(1), -impl::targetBits<T>())) break;
    }
    const ExpParts<T> e = expParts(-(dw(x) * x));
    if (e.underflow) return dw(T(0));
    return expValue(e) / (impl::sqrtPi<T>() * f);
}

// psi(z) for z > 0: the recurrence (DLMF 5.5.2) up to X, then the asymptotic series (DLMF 5.11.2).
template <class T>
Special<T> digammaPositive(DoubleWord<T> z) {
    using std::abs;
    const T shift = impl::stirlingShift<T>();
    DoubleWord<T> sum = dw(T(0));
    int n = 0;
    for (; z.hi < shift; ++n) {
        sum = sum + dw(T(1)) / z;
        z = z + T(1);
    }
    DoubleWord<T> s = logWord(z) - dw(T(0.5)) / z;
    const DoubleWord<T> z2 = z * z;
    DoubleWord<T> power = z2;
    DoubleWord<T> series = dw(T(0));
    int k = 1;
    for (const DoubleWord<T>& c : impl::stirlingWords<T>()) {  // B_2k / (2k z^2k) = c_k (2k - 1) / z^2k
        const DoubleWord<T> term = c * T(2 * k - 1) / power;
        if (impl::negligible(term, s)) break;
        series = series + term;
        power = power * z2;
        ++k;
    }
    s = s - series;
    Special<T> r;
    r.value = s - sum;
    r.scale = abs(s.hi) + abs(sum.hi) + T(n);
    return r;
}

// psi(x) for any x that is not a pole: the reflection psi(1 - x) - psi(x) = pi cot(pi x) (DLMF 5.5.4).
template <class T>
Special<T> digammaWord(const T& x) {
    using std::abs;
    if (x > 0) return digammaPositive(dw(x));
    const auto [s, c] = sinCosPi(x);
    const DoubleWord<T> cot = impl::word<T>(ConstantId::Pi) * c / s;
    const Special<T> g = digammaPositive(dw(T(1)) - x);
    Special<T> r;
    r.value = g.value - cot;
    r.scale = g.scale + abs(cot.hi);
    return r;
}

// psi'(z) for z > 0: every term positive (the recurrence, then B_2k / z^(2k+1)).
template <class T>
Special<T> trigammaPositive(DoubleWord<T> z) {
    const T shift = impl::stirlingShift<T>();
    DoubleWord<T> sum = dw(T(0));
    while (z.hi < shift) {
        sum = sum + dw(T(1)) / (z * z);
        z = z + T(1);
    }
    const DoubleWord<T> z2 = z * z;
    DoubleWord<T> s = dw(T(1)) / z + dw(T(0.5)) / z2;
    DoubleWord<T> power = z * z2;
    int k = 1;
    for (const DoubleWord<T>& c : impl::stirlingWords<T>()) {  // B_2k / z^(2k+1) = c_k 2k (2k - 1) / z^(2k+1)
        const DoubleWord<T> term = c * T((2 * k) * (2 * k - 1)) / power;
        if (impl::negligible(term, s)) break;
        s = s + term;
        power = power * z2;
        ++k;
    }
    Special<T> r;
    r.value = s + sum;
    return r;
}

// psi'(x) for any x that is not a pole: psi'(x) + psi'(1 - x) = pi^2 / sin^2(pi x).
template <class T>
Special<T> trigammaWord(const T& x) {
    using std::abs;
    if (x > 0) return trigammaPositive(dw(x));
    const DoubleWord<T> s = sinCosPi(x).first;
    const DoubleWord<T>& pi = impl::word<T>(ConstantId::Pi);
    const DoubleWord<T> first = (pi * pi) / (s * s);
    const DoubleWord<T> second = trigammaPositive(dw(T(1)) - x).value;
    Special<T> r;
    r.value = first - second;
    r.scale = abs(first.hi) + abs(second.hi);
    return r;
}

// ln|Gamma(x)| for any x that is not a pole: the reflection Gamma(x) Gamma(1-x) = pi / sin(pi x) (DLMF 5.5.3) for x < 0.
template <class T>
Special<T> lgammaWord(const T& x) {
    using std::abs;
    if (x > 0) return lgammaPositive(dw(x));
    const DoubleWord<T> s = sinCosPi(x).first;
    const DoubleWord<T> a = logWord(impl::word<T>(ConstantId::Pi)) - logWord(s.hi < 0 ? -s : s);  // ln(pi / |sin(pi x)|)
    const Special<T> g = lgammaPositive(dw(T(1)) - x);  // 1 - x as a double word: exact
    Special<T> r;
    r.value = a - g.value;
    r.scale = abs(a.hi) + g.scale;
    return r;
}

// sin(pi x) and cos(pi x) as double words. x = n + r with n the nearest integer and r = x - n exact, |r| <= 1/2, so
// sin(pi x) is accurate relative to itself even next to an integer (what the reflection formulas need).
template <class T>
std::pair<DoubleWord<T>, DoubleWord<T>> sinCosPi(const T& x) {
    using std::abs;
    using std::floor;
    using std::ldexp;
    // x + 1/2 is exact below 2^(p-1); beyond, every x is a whole number.
    const T n = abs(x) < ldexp(T(1), precisionBits<T>() - 1) ? T(floor(x + T(0.5))) : x;
    const T r = x - n;
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

// P(a, x) (lower) or Q(a, x) = 1 - P for a > 0, x > 0: the series (DLMF 8.7.1 form) or Legendre's continued fraction
// (DLMF 8.9.2, by Lentz), whichever converges fast; the other one is 1 minus it (Step 4: scale + 1).
template <class T>
Special<T> gammaPQ(const T& a, const T& x, bool lower, const std::atomic<bool>* cancel) {
    using std::abs;
    using std::ldexp;
    Special<T> r;
    const Special<T> lg = lgammaPositive(dw(a));
    const DoubleWord<T> t = logWord(dw(x)) * a;
    const DoubleWord<T> L = t - x - lg.value;  // ln(x^a e^-x / Gamma(a))
    const T scaleL = abs(t.hi) + x + lg.scale;
    const ExpParts<T> e = expParts(L);
    const DoubleWord<T> front = e.overflow || e.underflow ? dw(T(0)) : expValue(e);
    // The fraction converges slowly near x ~ a + 1 for small x: there the series also serves, Q being still above
    // 2^(-p/4) so that 1 - P keeps 7p/4 bits.
    const bool fraction = x >= a + 1 && x >= T(precisionBits<T>()) * impl::word<T>(ConstantId::Ln2).hi / 4;
    DoubleWord<T> direct;
    int n = 1;
    const auto stop = [&]() -> bool {
        if (n % 1024 == 0 && impl::cancelled(cancel)) {
            r.error = ErrorCode::Cancelled;
            return true;
        }
        if (n > impl::maxIterations) {
            r.error = ErrorCode::ArgumentTooLarge;
            return true;
        }
        return false;
    };
    if (!fraction) {  // x^a e^-x / Gamma(a + 1) sum x^k / ((a+1)…(a+k))
        DoubleWord<T> term = dw(T(1)), sum = dw(T(1)), ak = dw(a);
        for (;; ++n) {
            if (stop()) return r;
            ak = ak + T(1);
            term = term * x / ak;
            if (impl::negligible(term, sum)) break;
            sum = sum + term;
        }
        direct = front * sum / a;
    } else {
        const DoubleWord<T> tiny = dw((std::numeric_limits<T>::min)());
        DoubleWord<T> f = dw(x) + (T(1) - a), C = f, D = dw(T(0));
        for (;; ++n) {
            if (stop()) return r;
            const DoubleWord<T> an = -(dw(T(n)) * (T(n) - a));
            const DoubleWord<T> bn = dw(x) + (T(2 * n + 1) - a);
            D = bn + an * D;
            if (D.hi == 0) D = tiny;
            D = dw(T(1)) / D;
            C = bn + an / C;
            if (C.hi == 0) C = tiny;
            const DoubleWord<T> delta = C * D;
            f = f * delta;
            if (abs((delta - T(1)).hi) <= ldexp(T(1), -impl::targetBits<T>())) break;
        }
        direct = front / f;
    }
    const T s = abs(direct.hi) * (scaleL + T(n));
    if (fraction != lower) {  // the direct one is the one asked for
        r.value = direct;
        r.scale = s;
    } else {
        r.value = dw(T(1)) - direct;
        r.scale = s + 1;
    }
    return r;
}

}  // namespace calculate_core::detail
