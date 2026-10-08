#pragma once

#include "accuracy.hpp"

#include <boost/math/special_functions/beta.hpp>
#include <boost/math/special_functions/digamma.hpp>
#include <boost/math/special_functions/erf.hpp>
#include <boost/math/special_functions/gamma.hpp>
#include <boost/math/special_functions/trigamma.hpp>

#include <vector>

namespace test {

// Boost.Math (tests only) at a precision well above T's: it loses up to about 20 bits at 237 bits (measured),
// far less than the gap to the tested type.
template <class T> struct SpecialOracle { using type = Binary256; };
template <> struct SpecialOracle<Binary256> { using type = Binary512; };
template <> struct SpecialOracle<Binary512> { using type = Ruler; };
template <> struct SpecialOracle<Ruler> { using type = RulerCheck; };
template <class T> using SpecialOracleFor = typename SpecialOracle<T>::type;

// I_x(a, b) = x^a (1-x)^b / (a B(a, b)) * F(a+b, 1; a+1; x) (DLMF 8.17(ii)): positive terms, ratio -> x <= 1/2
// after the symmetry I_x(a, b) = 1 - I_{1-x}(b, a). Independent of the product's continued fraction; Boost's own
// ibeta loses bits at high precision.
template <class O>
O betaincReference(const O& a, const O& b, const O& x) {
    using std::ldexp;
    if (x == 0) return O(0);
    if (x == 1) return O(1);
    if (x > O(0.5)) return O(1) - betaincReference(b, a, O(1 - x));
    O term = 1, sum = 1;
    for (int n = 0; n < 1000000; ++n) {
        term = term * (a + b + n) / (a + n + 1) * x;
        sum += term;
        if (term < ldexp(sum, -precisionBits<O>() - 8)) break;
    }
    return pow(x, a) * pow(O(1 - x), b) / (a * boost::math::beta(a, b)) * sum;
}

// The x with I_x(a, b) = y nearest to `near`, by Newton steps at the oracle's precision.
template <class O>
O betaincinvReference(const O& a, const O& b, const O& y, O x) {
    using std::abs;
    using std::ldexp;
    for (int i = 0; i < 50; ++i) {
        const O slope = pow(x, a - 1) * pow(O(1 - x), b - 1) / boost::math::beta(a, b);
        const O step = (betaincReference(a, b, x) - y) / slope;
        x -= step;
        if (abs(step) <= ldexp(abs(x), 4 - precisionBits<O>())) break;
    }
    return x;
}

template <class O>
O specialOracle(FunctionId id, [[maybe_unused]] const std::vector<O>& a) {
    switch (id) {
    // later cycles add one case each here
    case FunctionId::Lgamma: return boost::math::lgamma(a[0]);
    case FunctionId::Gamma: return boost::math::tgamma(a[0]);
    case FunctionId::Digamma: return boost::math::digamma(a[0]);
    case FunctionId::Trigamma: return boost::math::trigamma(a[0]);
    case FunctionId::Beta: return boost::math::beta(a[0], a[1]);
    case FunctionId::Erf: return boost::math::erf(a[0]);
    case FunctionId::Erfc: return boost::math::erfc(a[0]);
    case FunctionId::Erfinv: return boost::math::erf_inv(a[0]);
    case FunctionId::Erfcinv: return boost::math::erfc_inv(a[0]);
    case FunctionId::GammaP: return boost::math::gamma_p(a[0], a[1]);
    case FunctionId::GammaQ: return boost::math::gamma_q(a[0], a[1]);
    case FunctionId::Igamma: return boost::math::tgamma(a[0], a[1]);
    case FunctionId::GammaInc: return boost::math::tgamma_lower(a[0], a[1]);
    case FunctionId::Betainc: return betaincReference(a[0], a[1], a[2]);  // arguments (a, b, x)
    default: return O(0);
    }
}

// |computed - exact| in units of u * max(|exact|, min(), u * scale): the library claim with its floor.
template <class T, class O>
double errorInUScaled(const T& computed, const O& exact, const T& scale) {
    using std::abs;
    const O u = exactCast<O>(unitRoundoff<T>());
    O floor = exactCast<O>((std::numeric_limits<T>::min)());
    if (abs(exact) > floor) floor = abs(exact);
    const O cancelled = u * exactCast<O>(scale);
    if (cancelled > floor) floor = cancelled;
    return O(abs(exactCast<O>(computed) - exact) / (u * floor)).template convert_to<double>();
}

// Every sample must compute without error and within the claim (its floor included).
template <class T>
void expectSpecialWithinClaim(FunctionId id, const std::function<std::vector<T>(std::mt19937_64&)>& sample) {
    using O = SpecialOracleFor<T>;
    std::mt19937_64 rng(static_cast<unsigned>(id) + 300);
    for (int i = 0; i < samplesFor<T>(); ++i) {
        const std::vector<T> args = sample(rng);
        const Applied<T> r = applyFunction<T>(id, args);
        ASSERT_FALSE(r.error) << "sample " << i;
        std::vector<O> exactArgs;
        for (const T& a : args) exactArgs.push_back(exactCast<O>(a));
        const double error = errorInUScaled(r.value, specialOracle<O>(id, exactArgs), r.scale);
        EXPECT_LE(error, claimedFactor(id)) << "sample " << i << ": " << error << " u";
    }
}

}  // namespace test
