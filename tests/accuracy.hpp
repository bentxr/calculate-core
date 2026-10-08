#pragma once

#include "functions.hpp"
#include "test_support.hpp"

#include <functional>
#include <utility>

namespace test {

using Oracle = RulerCheck;  // Boost's own functions at 2017 bits: an independent reference (tests only)

// Samples per accuracy test: fewer for the slow software types.
template <class T>
int samplesFor() {
    if constexpr (std::is_floating_point_v<T>) return 100;
    else if constexpr (std::is_same_v<T, Binary128>) return 30;
    else if constexpr (std::is_same_v<T, Binary256>) return 12;
    else return 6;
}

// Full-precision values in [lo, hi].
template <class T>
T uniform(std::mt19937_64& rng, double lo, double hi) {
    using std::abs;
    const T m = abs(randomFinite<T>(rng, 0));  // [1, 2)
    return T(lo) + (T(hi) - T(lo)) * (m - 1);
}

// Full-precision values with a binary exponent in [e1, e2], clamped to the type's range.
template <class T>
T logUniform(std::mt19937_64& rng, int e1, int e2) {
    using std::abs;
    using std::ldexp;
    e1 = std::max(e1, minExponent<T>() / 2);
    e2 = std::min(e2, maxExponent<T>() - 2);
    std::uniform_int_distribution<int> exponent(e1, e2);
    return ldexp(abs(randomFinite<T>(rng, 0)), exponent(rng));
}

template <class T>
T randomSign(std::mt19937_64& rng, const T& x) { return (rng() & 1) ? T(-x) : x; }

// |computed - exact| in units of u * max(|exact|, min()).
template <class T>
double errorInU(const T& computed, const Oracle& exact) {
    using std::abs;
    const Oracle floor = exactCast<Oracle>((std::numeric_limits<T>::min)());
    const Oracle magnitude = abs(exact) > floor ? Oracle(abs(exact)) : floor;
    const Oracle error = abs(exactCast<Oracle>(computed) - exact);
    return Oracle(error / (exactCast<Oracle>(unitRoundoff<T>()) * magnitude)).template convert_to<double>();
}

inline Oracle oracle(FunctionId id, const Oracle& x, const Oracle& y) {
    switch (id) {
    case FunctionId::Exp: return exp(x);
    case FunctionId::Ln: return log(x);
    case FunctionId::Log10: return log(x) / log(Oracle(10));
    case FunctionId::LogBase: return log(x) / log(y);
    case FunctionId::Sin: return sin(x);
    case FunctionId::Cos: return cos(x);
    case FunctionId::Tan: return tan(x);
    case FunctionId::Asin: return asin(x);
    case FunctionId::Acos: return acos(x);
    case FunctionId::Atan: return atan(x);
    case FunctionId::Sinh: return sinh(x);
    case FunctionId::Cosh: return cosh(x);
    case FunctionId::Tanh: return tanh(x);
    case FunctionId::Asinh: return x < 0 ? Oracle(-log(-x + sqrt(x * x + 1))) : Oracle(log(x + sqrt(x * x + 1)));
    case FunctionId::Acosh: return log(x + sqrt(x * x - 1));
    case FunctionId::Atanh: return log((1 + x) / (1 - x)) / 2;
    case FunctionId::Power: return pow(x, y);
    case FunctionId::Cbrt: return x < 0 ? Oracle(-exp(log(-x) / 3)) : Oracle(exp(log(x) / 3));
    case FunctionId::Root: return x < 0 ? Oracle(-exp(log(-x) / y)) : Oracle(exp(log(x) / y));
    case FunctionId::Sqrt: return sqrt(x);
    case FunctionId::Abs: return abs(x);
    case FunctionId::Csch: return 1 / sinh(x);
    case FunctionId::Acot: return x == 0 ? Oracle(acos(Oracle(-1)) / 2) : Oracle(atan(1 / x));
    case FunctionId::Atan2: return atan2(x, y);  // arguments (y, x): the first one is the ordinate
    case FunctionId::Hypot: return sqrt(x * x + y * y);
    case FunctionId::Sinc: return x == 0 ? Oracle(1) : Oracle(sin(x) / x);
    default: return Oracle(0);
    }
}

// Every sample must compute without error and within the function's claimed accuracy.
template <class T>
void expectWithinClaim(FunctionId id, const std::function<std::pair<T, T>(std::mt19937_64&)>& sample) {
    std::mt19937_64 rng(static_cast<unsigned>(id) + 100);
    for (int i = 0; i < samplesFor<T>(); ++i) {
        const auto [x, y] = sample(rng);
        std::vector<T> args{x};
        if (functionInfo(id).minArgs == 2) args.push_back(y);
        const Applied<T> r = applyFunction<T>(id, args);
        ASSERT_FALSE(r.error) << "sample " << i;
        const double error = errorInU(r.value, oracle(id, exactCast<Oracle>(x), exactCast<Oracle>(y)));
        EXPECT_LE(error, claimedFactor(id)) << "sample " << i << ": " << error << " u";
    }
}

}  // namespace test
