#include "test_support.hpp"

using namespace calculate_core::detail;

namespace {

// Integer fixed-point series, independent of the table: result ~ value * 2^bits.
Integer arctanInverse(unsigned x, int bits, bool hyperbolic) {
    Integer power = (Integer(1) << bits) / x;  // 2^bits / x^(2k+1)
    const Integer x2 = Integer(x) * x;
    Integer sum = 0;
    for (unsigned k = 0; power != 0; ++k) {
        const Integer term = power / (2 * k + 1);
        sum += (hyperbolic || k % 2 == 0) ? term : Integer(-term);
        power /= x2;
    }
    return sum;
}

Integer seriesValue(ConstantId id, int bits) {
    const Integer pi = 16 * arctanInverse(5, bits, false) - 4 * arctanInverse(239, bits, false);
    const Integer ln2 = 2 * arctanInverse(3, bits, true);
    switch (id) {
    case ConstantId::Pi: return pi;
    case ConstantId::TwoOverPi: return (Integer(1) << (2 * bits + 1)) / pi;
    case ConstantId::Ln2: return ln2;
    case ConstantId::Ln10: return 3 * ln2 + 2 * arctanInverse(9, bits, true);  // 10 = 8 * 1.25
    case ConstantId::E: {
        Integer term = Integer(1) << bits, sum = 0;
        for (unsigned k = 1; term != 0; ++k) { sum += term; term /= k; }
        return sum;
    }
    default: break;  // the others are checked by their own tests
    }
    return 0;
}

}  // namespace

TEST(Constants, TableMatchesIndependentSeries) {
    const int guard = 64;
    for (ConstantId id : {ConstantId::Pi, ConstantId::TwoOverPi, ConstantId::Ln2,
                          ConstantId::Ln10, ConstantId::E}) {
        const Integer series = seriesValue(id, constantFractionBits + guard) >> guard;
        const Integer table = constantMantissa(id);
        const Integer difference = series > table ? Integer(series - table) : Integer(table - series);
        EXPECT_LE(difference, 2) << static_cast<int>(id);
    }
}

TEST(Constants, DoubleValuesAreCorrectlyRounded) {
    EXPECT_EQ(constantValue<double>(ConstantId::Pi), 0x1.921fb54442d18p+1);
    EXPECT_EQ(constantValue<double>(ConstantId::TwoOverPi), 0x1.45f306dc9c883p-1);
    EXPECT_EQ(constantValue<double>(ConstantId::Ln2), 0x1.62e42fefa39efp-1);
    EXPECT_EQ(constantValue<double>(ConstantId::Ln10), 0x1.26bb1bbb55516p+1);
    EXPECT_EQ(constantValue<double>(ConstantId::E), 0x1.5bf0a8b145769p+1);
}

TEST(Constants, FloatValuesAreCorrectlyRounded) {
    EXPECT_EQ(constantValue<float>(ConstantId::Pi), 0x1.921fb6p+1f);
    EXPECT_EQ(constantValue<float>(ConstantId::TwoOverPi), 0x1.45f306p-1f);
    EXPECT_EQ(constantValue<float>(ConstantId::Ln2), 0x1.62e430p-1f);
    EXPECT_EQ(constantValue<float>(ConstantId::Ln10), 0x1.26bb1cp+1f);
    EXPECT_EQ(constantValue<float>(ConstantId::E), 0x1.5bf0a8p+1f);
}

TEST(Constants, RationalIsTheTableValue) {
    EXPECT_EQ(constantRational(ConstantId::Pi),
              Rational(constantMantissa(ConstantId::Pi)) / Rational(Integer(1) << constantFractionBits));
}

namespace {

// floor(constant * 2^bits) with exact integers only.
Integer algebraicValue(ConstantId id, int bits) {
    switch (id) {
    case ConstantId::Sqrt2: return sqrt(Integer(2) << (2 * bits));
    case ConstantId::Phi: return ((Integer(1) << bits) + sqrt(Integer(5) << (2 * bits))) >> 1;
    case ConstantId::Plastic: {  // the real root of x^3 = x + 1: the largest X with X^3 - X 2^(2b) - 2^(3b) <= 0
        const Integer square = Integer(1) << (2 * bits);
        const Integer cube = Integer(1) << (3 * bits);
        Integer lo = Integer(1) << bits;
        Integer hi = Integer(2) << bits;
        while (hi - lo > 1) {
            const Integer mid = (lo + hi) >> 1;
            if (mid * mid * mid - mid * square - cube <= 0) lo = mid;
            else hi = mid;
        }
        return lo;
    }
    default: return 0;
    }
}

}  // namespace

TEST(Constants, AlgebraicConstantsAreTheirTruncatedValues) {
    for (ConstantId id : {ConstantId::Sqrt2, ConstantId::Phi, ConstantId::Plastic})
        EXPECT_EQ(constantMantissa(id), algebraicValue(id, constantFractionBits)) << static_cast<int>(id);
}

TEST(Constants, AlgebraicConstantsInDoubleAndFloat) {
    EXPECT_EQ(constantValue<double>(ConstantId::Sqrt2), 0x1.6a09e667f3bcdp+0);
    EXPECT_EQ(constantValue<double>(ConstantId::Phi), 0x1.9e3779b97f4a8p+0);
    EXPECT_EQ(constantValue<double>(ConstantId::Plastic), 0x1.5320b74eca44bp+0);
    EXPECT_EQ(constantValue<float>(ConstantId::Sqrt2), 0x1.6a09e6p+0f);
    EXPECT_EQ(constantValue<float>(ConstantId::Phi), 0x1.9e377ap+0f);
    EXPECT_EQ(constantValue<float>(ConstantId::Plastic), 0x1.5320b8p+0f);
}

namespace {

// Euler-Mascheroni times 2^bits, Brent-McMillan with n = 2^11: gamma = U/V - ln n, where
// B_k = B_(k-1) n^2 / k^2 and A_k = (A_(k-1) n^2 / k + B_k) / k, A_0 = -ln n, B_0 = 1.
Integer eulerGamma(int bits) {
    const unsigned n = 1u << 11;
    Integer a = -11 * (2 * arctanInverse(3, bits, true));  // -ln(2^11), with ln 2 = 2 atanh(1/3)
    Integer b = Integer(1) << bits;
    Integer u = a;
    Integer v = b;
    for (unsigned k = 1; a != 0 || b != 0; ++k) {
        b = b * n * n / (Integer(k) * k);
        a = (a * n * n / k + b) / k;
        u += a;
        v += b;
    }
    return (u << bits) / v;
}

}  // namespace

TEST(Constants, EulerGammaMatchesBrentMcMillan) {
    const int guard = 64;
    const Integer series = eulerGamma(constantFractionBits + guard) >> guard;
    const Integer table = constantMantissa(ConstantId::EulerGamma);
    const Integer difference = series > table ? Integer(series - table) : Integer(table - series);
    EXPECT_LE(difference, 2);
    EXPECT_EQ(constantValue<double>(ConstantId::EulerGamma), 0x1.2788cfc6fb619p-1);
    EXPECT_EQ(constantValue<float>(ConstantId::EulerGamma), 0x1.2788dp-1f);
}

namespace {

// Catalan * 2^bits: G = 1/2 sum_k (k!)^2 2^k / (2k+1)! * sum_(j<=k) 1/(2j+1).
Integer catalan(int bits) {
    Integer sum = 0;
    Integer term = Integer(1) << bits;  // (k!)^2 2^k / (2k+1)!, scaled
    Integer odd = Integer(1) << bits;   // sum_(j<=k) 1/(2j+1), scaled
    for (unsigned k = 0; term != 0;) {
        sum += (term * odd) >> bits;
        ++k;
        term = term * k * k * 2 / (Integer(2 * k) * (2 * k + 1));
        odd += (Integer(1) << bits) / (2 * k + 1);
    }
    return sum >> 1;
}

// zeta(3) * 2^bits: 1/64 sum_n (-1)^n (n!)^10 (205 n^2 + 250 n + 77) / ((2n+1)!)^5.
Integer apery(int bits) {
    Integer sum = 0;
    Integer factorial = 1;     // n!
    Integer oddFactorial = 1;  // (2n+1)!
    for (unsigned n = 0;; ) {
        const Integer term = (pow(factorial, 10) * (205 * n * n + 250 * n + 77) << bits) / (64 * pow(oddFactorial, 5));
        if (term == 0) break;
        sum += n % 2 == 0 ? term : Integer(-term);
        ++n;
        factorial *= n;
        oddFactorial *= Integer(2 * n) * (2 * n + 1);
    }
    return sum;
}

// e^(-x / 2^bits) * 2^bits for 0 <= x < 2^bits, by its series.
Integer expNegative(const Integer& x, int bits) {
    Integer sum = 0;
    Integer term = Integer(1) << bits;
    for (unsigned k = 0; term != 0;) {
        sum += k % 2 == 0 ? term : Integer(-term);
        ++k;
        term = term * x / ((Integer(1) << bits) * k);
    }
    return sum;
}

}  // namespace

TEST(Constants, CatalanAndAperyMatchOtherSeries) {
    const int guard = 64;
    for (ConstantId id : {ConstantId::Catalan, ConstantId::Apery}) {
        const int bits = constantFractionBits + guard;
        const Integer series = (id == ConstantId::Catalan ? catalan(bits) : apery(bits)) >> guard;
        const Integer table = constantMantissa(id);
        const Integer difference = series > table ? Integer(series - table) : Integer(table - series);
        EXPECT_LE(difference, 2) << static_cast<int>(id);
    }
}

TEST(Constants, OmegaIsTheTruncatedRootOfXEqualsExpMinusX) {
    const int guard = 64;
    const int bits = constantFractionBits + guard;
    const Integer t = constantMantissa(ConstantId::Omega) << guard;
    const Integer next = t + (Integer(1) << guard);  // one unit of the table further
    EXPECT_GE(expNegative(t, bits) - t, 0);
    EXPECT_LT(expNegative(next, bits) - next, 0);
}

TEST(Constants, SeriesConstantsInDoubleAndFloat) {
    EXPECT_EQ(constantValue<double>(ConstantId::Catalan), 0x1.d4f9713e8135dp-1);
    EXPECT_EQ(constantValue<double>(ConstantId::Apery), 0x1.33ba004f00621p+0);
    EXPECT_EQ(constantValue<double>(ConstantId::Omega), 0x1.22609af8e9657p-1);
    EXPECT_EQ(constantValue<float>(ConstantId::Catalan), 0x1.d4f972p-1f);
    EXPECT_EQ(constantValue<float>(ConstantId::Apery), 0x1.33bap+0f);
    EXPECT_EQ(constantValue<float>(ConstantId::Omega), 0x1.22609ap-1f);
}
