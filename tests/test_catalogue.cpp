#include "accuracy.hpp"
#include "engine.hpp"
#include "parser.hpp"

#include <calculate-core/calculate-core.hpp>

#include <functional>
#include <string>

using namespace calculate_core;
using namespace calculate_core::detail;
using std::ldexp;
using test::logUniform;
using test::randomSign;
using test::uniform;

namespace {

// A readable form of a tree: (op arg ...), literals as written, operators by symbol.
std::string shape(const Ast& ast, int i) {
    const Node& n = ast.nodes[static_cast<std::size_t>(i)];
    if (n.function == FunctionId::Literal) return n.text;
    std::string op;
    switch (n.function) {
    case FunctionId::Add: op = "+"; break;
    case FunctionId::Subtract: op = "-"; break;
    case FunctionId::Multiply: op = "*"; break;
    case FunctionId::Divide: op = "/"; break;
    case FunctionId::Negate: op = "neg"; break;
    case FunctionId::Power: op = "^"; break;
    case FunctionId::Square: op = "sq"; break;
    case FunctionId::LogBase: op = "logb"; break;
    default: op = std::string(functionInfo(n.function).name);
    }
    if (n.args.empty()) return op;
    std::string s = "(" + op;
    for (const int a : n.args) s += " " + shape(ast, a);
    return s + ")";
}

std::string tree(std::string_view text, AngleUnit angle = AngleUnit::Radians) {
    const Parsed p = parse(text, angle);
    if (p.error) return "error: " + p.error->message;
    return shape(p.ast, p.ast.root());
}

[[maybe_unused]] Result inType(std::string_view text, NumberType type, AngleUnit angle = AngleUnit::Radians) {
    Options options;
    options.type = type;
    options.angle = angle;
    return evaluate(text, options);
}

// x written out exactly (every digit), so a literal made from it carries no input error.
template <class T>
std::string exactly(const T& x) {
    const DecimalDigits d = exactDigits(x);
    return (d.negative ? "-0." : "0.") + d.digits + "e" + std::to_string(d.exponent10 + 1);
}

// A lowered function's bound covers its true error (against the oracle) on random arguments; an argument whose
// error reaches an edge may be refused instead, with that code.
template <class T>
void expectBoundCoversOracle(const std::string& name, const std::function<test::Oracle(const test::Oracle&)>& f,
                             const std::function<T(std::mt19937_64&)>& sample) {
    using std::abs;
    std::mt19937_64 rng(static_cast<unsigned>(std::hash<std::string>{}(name) & 0xffffu));
    for (int i = 0; i < test::samplesFor<T>(); ++i) {
        const T x = sample(rng);
        const std::string text = name + "(" + exactly(x) + ")";
        const Parsed p = parse(text, AngleUnit::Radians);
        ASSERT_FALSE(p.error) << text;
        const Evaluation<T> ev = detail::evaluate<T>(p.ast);
        if (ev.error) {
            EXPECT_EQ(ev.error->code, ErrorCode::ArgumentNearEdge) << text;
            continue;
        }
        const Ruler error = fromRational<Ruler>(abs(toRational(ev.value) - toRational(f(exactCast<test::Oracle>(x)))));
        EXPECT_TRUE(test::covers(ev.report.bound, error)) << text << ": error " << formatScientific(error)
                                                          << " > bound " << formatScientific(ev.report.bound);
    }
}

}  // namespace

TEST(Catalogue, OtherSpellingsOfTheInverseFunctions) {
    EXPECT_EQ(tree("arcsin(1)"), "(asin 1)");
    EXPECT_EQ(tree("arsinh(1)"), "(asinh 1)");
    EXPECT_EQ(tree("arcosh(2)"), "(acosh 2)");
    EXPECT_EQ(tree("artanh(0)"), "(atanh 0)");
    EXPECT_EQ(tree("arccos(1)"), "(acos 1)");  // already a Spanish spelling, also English
    EXPECT_EQ(evaluate("arcsin(2)").error->message, "arcsin is not defined for this argument");
}

TEST(Catalogue, LoweredFunctionsBecomeExistingNodes) {
    EXPECT_EQ(tree("log2(8)"), "(logb 8 2)");
    EXPECT_EQ(tree("exp2(3)"), "(^ 2 3)");
    EXPECT_EQ(tree("exp10(3)"), "(^ 10 3)");
    EXPECT_EQ(tree("sq(3)"), "(sq 3)");
    EXPECT_EQ(tree("sqrtpi(2)"), "(sqrt (* 2 pi))");
    EXPECT_EQ(tree("log2(8, 2)"), "error: log2 takes 1 argument");
    EXPECT_EQ(tree("sq"), "error: sq needs its arguments in parentheses: sq(…)");
}

TEST(Catalogue, LoweredFunctionsSpeakUnderTheirOwnName) {
    EXPECT_EQ(evaluate("sqrtpi(-1)").error->message, "sqrtpi is not defined for this argument");
    EXPECT_EQ(evaluate("log2(0)").error->message, "log2 is not defined for this argument");
    EXPECT_EQ(inType("sqrtpi(1)", NumberType::Exact).error->message,
              "sqrtpi is not available in exact arithmetic: its result is irrational");
    const Parsed p = parse("sq(3)", AngleUnit::Radians);
    ASSERT_FALSE(p.error);
    EXPECT_TRUE(p.ast.nodes.back().lowered);
    EXPECT_EQ(p.ast.nodes.back().written, "sq");
    EXPECT_FALSE(parse("3²", AngleUnit::Radians).ast.nodes.back().lowered);
}

TEST(Catalogue, LoweredFunctionsKeepTheErrorContract) {
    const Result eight = evaluate("log2(8)");
    ASSERT_FALSE(eight.error);
    EXPECT_EQ(eight.value.digits, "3");
    const Result thousand = evaluate("exp10(3)");
    ASSERT_FALSE(thousand.error);
    EXPECT_EQ(thousand.value.digits, "1");
    EXPECT_EQ(thousand.value.exponent10, 3);
    EXPECT_EQ(thousand.bound, "0");  // 10^3 is checked exactly, as an integer power
    EXPECT_EQ(inType("exp2(3)", NumberType::Exact).exact->numerator, "8");
    EXPECT_EQ(inType("sq(2/3)", NumberType::Exact).exact->numerator, "4");
    EXPECT_EQ(inType("exp2(1/2)", NumberType::Exact).error->code, ErrorCode::IrrationalResult);
}

TEST(Catalogue, LoweredFunctionsAreListed) {
    for (const char* name : {"log2", "exp2", "exp10", "sq", "sqrtpi"}) {
        bool found = false;
        for (const FunctionDescription& f : functions())
            if (f.name == name) found = f.minArgs == 1 && f.maxArgs == 1;
        EXPECT_TRUE(found) << name;
    }
}

template <class T>
class CatalogueKernelTest : public ::testing::Test {};
TYPED_TEST_SUITE(CatalogueKernelTest, test::FloatingTypes, test::TypeNames);

TYPED_TEST(CatalogueKernelTest, LoweredPowersAndLogarithmsCoverTheTruth) {
    using T = TypeParam;
    using O = test::Oracle;
    expectBoundCoversOracle<T>("log2", [](const O& x) { return O(log(x) / log(O(2))); },
                               [](std::mt19937_64& rng) { return logUniform<T>(rng, -60, 60); });
    expectBoundCoversOracle<T>("exp2", [](const O& x) { return O(pow(O(2), x)); },
                               [](std::mt19937_64& rng) { return uniform<T>(rng, -60, 60); });
    expectBoundCoversOracle<T>("sqrtpi", [](const O& x) { return O(sqrt(x * acos(O(-1)))); },
                               [](std::mt19937_64& rng) { return logUniform<T>(rng, -30, 30); });
}


// A double word's value against the oracle, in units of u^2 relative to the exact value.
template <class T>
double errorInU2(const DoubleWord<T>& w, const test::Oracle& exact) {
    using O = test::Oracle;
    const O u = exactCast<O>(unitRoundoff<T>());
    const O value = exactCast<O>(w.hi) + exactCast<O>(w.lo);
    return O(abs(value - exact) / (u * u * abs(exact))).template convert_to<double>();
}

TYPED_TEST(CatalogueKernelTest, DoubleWordSineCosineAndHyperbolic) {
    using T = TypeParam;
    using O = test::Oracle;
    std::mt19937_64 rng(21);
    for (int i = 0; i < test::samplesFor<T>(); ++i) {
        const T x = randomSign(rng, logUniform<T>(rng, -10, 40));
        const auto sc = sinCosWord(x);
        ASSERT_TRUE(sc);
        EXPECT_LE(errorInU2(sc->first, sin(exactCast<O>(x))), 64.0) << i;
        EXPECT_LE(errorInU2(sc->second, cos(exactCast<O>(x))), 64.0) << i;
        const T h = uniform<T>(rng, 0, 20);
        EXPECT_LE(errorInU2(sinhCoshWord(h, true), sinh(exactCast<O>(h))), 64.0) << i;
        EXPECT_LE(errorInU2(sinhCoshWord(h, false), cosh(exactCast<O>(h))), 64.0) << i;
    }
    EXPECT_FALSE(sinCosWord(ldexp(T(1), std::min(1024, maxExponent<T>()))).has_value() && maxExponent<T>() >= 1024);
}

TEST(Catalogue, ReciprocalTrigonometryIsWrittenWithTheFunctionsWeHave) {
    EXPECT_EQ(tree("sec(2)"), "(/ 1 (cos 2))");
    EXPECT_EQ(tree("csc(2)"), "(/ 1 (sin 2))");
    EXPECT_EQ(tree("cot(2)"), "(/ 1 (tan 2))");
    EXPECT_EQ(tree("sec(60)", AngleUnit::Degrees), "(/ 1 (cos (* 60 (/ pi 180))))");  // the angle unit applies inside
    EXPECT_EQ(evaluate("sec(0)").value.digits, "1");
    EXPECT_EQ(evaluate("csc(0)").error->code, ErrorCode::DomainError);
    EXPECT_EQ(evaluate("csc(0)").error->message, "csc is not defined for this argument");
    EXPECT_EQ(evaluate("cot(0)").error->message, "cot is not defined for this argument");
    const Result pole = inType("sec(90)", NumberType::Double, AngleUnit::Degrees);
    ASSERT_TRUE(pole.error);
    EXPECT_EQ(pole.error->code, ErrorCode::ArgumentNearEdge);  // cos(90°) is 0 within its error
    EXPECT_EQ(pole.error->message.rfind("sec ", 0), 0u);
    EXPECT_EQ(inType("cot(1)", NumberType::Exact).error->code, ErrorCode::NotAvailableInExact);
}

TYPED_TEST(CatalogueKernelTest, ReciprocalTrigonometryCoversTheTruth) {
    using T = TypeParam;
    using O = test::Oracle;
    const auto sample = [](std::mt19937_64& rng) { return randomSign(rng, logUniform<T>(rng, -20, 20)); };
    expectBoundCoversOracle<T>("sec", [](const O& x) { return O(1 / cos(x)); }, sample);
    expectBoundCoversOracle<T>("csc", [](const O& x) { return O(1 / sin(x)); }, sample);
    expectBoundCoversOracle<T>("cot", [](const O& x) { return O(cos(x) / sin(x)); }, sample);
}

TEST(Catalogue, HyperbolicReciprocalsAreWrittenWithTheFunctionsWeHave) {
    EXPECT_EQ(tree("coth(2)"), "(/ 1 (tanh 2))");
    EXPECT_EQ(tree("sech(2)"), "(/ (* 2 (exp (neg (abs 2)))) (+ 1 (exp (neg (* 2 (abs 2))))))");
    EXPECT_EQ(evaluate("sech(0)").value.digits, "1");
    EXPECT_FALSE(evaluate("sech(1000)").error);  // tiny, never an overflow
    EXPECT_EQ(evaluate("coth(0)").error->message, "coth is not defined for this argument");
}

TYPED_TEST(CatalogueKernelTest, HyperbolicReciprocals) {
    using T = TypeParam;
    using O = test::Oracle;
    const double range = std::min(700.0, 0.69 * maxExponent<T>());
    test::expectWithinClaim<T>(FunctionId::Csch, [range](auto& rng) { return std::pair<T, T>{uniform<T>(rng, -range, range), T(0)}; });
    test::expectWithinClaim<T>(FunctionId::Csch, [](auto& rng) { return std::pair<T, T>{randomSign(rng, logUniform<T>(rng, -30, 0)), T(0)}; });
    EXPECT_EQ(applyFunction<T>(FunctionId::Csch, {T(0)}).error.value_or(ErrorCode::Cancelled), ErrorCode::DomainError);
    EXPECT_EQ(applyFunction<T>(FunctionId::Csch, {ldexp(T(1), maxExponent<T>() / 2)}).value, T(0));  // underflows
    const auto sample = [range](std::mt19937_64& rng) { return uniform<T>(rng, -range, range); };
    expectBoundCoversOracle<T>("sech", [](const O& x) { return O(1 / cosh(x)); }, sample);
    expectBoundCoversOracle<T>("coth", [](const O& x) { return O(cosh(x) / sinh(x)); }, sample);
}

TEST(Catalogue, InverseSecantAndCosecantAreWrittenWithTheFunctionsWeHave) {
    EXPECT_EQ(tree("asec(2)"), "(acos (/ 1 2))");
    EXPECT_EQ(tree("acsc(2)"), "(asin (/ 1 2))");
    EXPECT_EQ(tree("asec(2)", AngleUnit::Degrees), "(* (acos (/ 1 2)) (/ 180 pi))");
    EXPECT_EQ(tree("arcsec(2)"), tree("asec(2)"));
    EXPECT_EQ(tree("arccsc(2)"), tree("acsc(2)"));
    EXPECT_EQ(tree("arccot(2)"), "(acot 2)");
    EXPECT_EQ(evaluate("asec(0.5)").error->message, "asec is not defined for this argument");
    EXPECT_EQ(evaluate("acsc(0)").error->message, "acsc is not defined for this argument");
    EXPECT_EQ(evaluate("arcsec(0.5)").error->message, "arcsec is not defined for this argument");
}

TEST(Catalogue, TheInverseCotangentJumpsAtZero) {
    EXPECT_EQ(evaluate("acot(0.1+0.2-0.3)").error->code, ErrorCode::ArgumentNearJump);
    EXPECT_FALSE(evaluate("acot(0)").error);    // exactly 0: π/2
    EXPECT_FALSE(evaluate("acot(0.3)").error);  // far from it
}

TYPED_TEST(CatalogueKernelTest, InverseReciprocalTrigonometry) {
    using T = TypeParam;
    using O = test::Oracle;
    test::expectWithinClaim<T>(FunctionId::Acot, [](auto& rng) { return std::pair<T, T>{randomSign(rng, logUniform<T>(rng, -30, 30)), T(0)}; });
    EXPECT_EQ(applyFunction<T>(FunctionId::Acot, {T(0)}).value, constantValue<T>(ConstantId::Pi) / 2);
    EXPECT_EQ(applyFunction<T>(FunctionId::Acot, {T(-2)}).value, -applyFunction<T>(FunctionId::Acot, {T(2)}).value);  // odd
    const auto outside = [](std::mt19937_64& rng) { return randomSign(rng, T(T(1) + logUniform<T>(rng, -40, 30))); };
    expectBoundCoversOracle<T>("asec", [](const O& x) { return O(acos(1 / x)); }, outside);
    expectBoundCoversOracle<T>("acsc", [](const O& x) { return O(asin(1 / x)); }, outside);
}

TEST(Catalogue, InverseHyperbolicReciprocalsAreWrittenWithTheFunctionsWeHave) {
    EXPECT_EQ(tree("asech(0.5)"), "(acosh (/ 1 0.5))");
    EXPECT_EQ(tree("acsch(2)"), "(asinh (/ 1 2))");
    EXPECT_EQ(tree("acoth(2)"), "(atanh (/ 1 2))");
    EXPECT_EQ(tree("asech(0.5)", AngleUnit::Degrees), "(acosh (/ 1 0.5))");  // not angles
    EXPECT_EQ(tree("arsech(0.5)"), tree("asech(0.5)"));
    EXPECT_EQ(tree("arcsch(2)"), tree("acsch(2)"));
    EXPECT_EQ(tree("arcoth(2)"), tree("acoth(2)"));
    for (const char* text : {"asech(0)", "asech(1.5)", "acsch(0)", "acoth(1)", "acoth(0.5)"})
        EXPECT_EQ(evaluate(text).error->code, ErrorCode::DomainError) << text;
    EXPECT_EQ(evaluate("acoth(0.5)").error->message, "acoth is not defined for this argument");
    EXPECT_EQ(evaluate("asech(1)").value.digits, "0");
}

TYPED_TEST(CatalogueKernelTest, InverseHyperbolicReciprocalsCoverTheTruth) {
    using T = TypeParam;
    using O = test::Oracle;
    expectBoundCoversOracle<T>("asech", [](const O& x) { return O(log((1 + sqrt(1 - x * x)) / x)); },
                               [](std::mt19937_64& rng) { return uniform<T>(rng, 0.001, 0.999); });
    expectBoundCoversOracle<T>("acsch", [](const O& x) { const O t = 1 / x; return t < 0 ? O(-log(-t + sqrt(t * t + 1))) : O(log(t + sqrt(t * t + 1))); },
                               [](std::mt19937_64& rng) { return randomSign(rng, logUniform<T>(rng, -60, 60)); });
    // 1 + 2^-k stays above 1 only for k below T's precision (in float 1 + 2^-40 is 1, outside acoth's domain).
    expectBoundCoversOracle<T>("acoth", [](const O& x) { return O(log((x + 1) / (x - 1)) / 2); },
                               [](std::mt19937_64& rng) { return randomSign(rng, T(T(1) + logUniform<T>(rng, std::max(-40, 2 - precisionBits<T>()), 30))); });
}

TYPED_TEST(CatalogueKernelTest, FourQuadrantArctangent) {
    using T = TypeParam;
    test::expectWithinClaim<T>(FunctionId::Atan2, [](auto& rng) {
        return std::pair<T, T>{randomSign(rng, logUniform<T>(rng, -30, 30)), randomSign(rng, logUniform<T>(rng, -30, 30))};
    });
    const int far = maxExponent<T>() / 2;  // ratios that overflow or underflow T
    test::expectWithinClaim<T>(FunctionId::Atan2, [far](auto& rng) {
        return std::pair<T, T>{randomSign(rng, logUniform<T>(rng, far - 8, far)), randomSign(rng, logUniform<T>(rng, -far, 8 - far))};
    });
    const T pi = constantValue<T>(ConstantId::Pi);
    EXPECT_EQ(applyFunction<T>(FunctionId::Atan2, {T(1), T(1)}).value, pi / 4);
    EXPECT_EQ(applyFunction<T>(FunctionId::Atan2, {T(0), T(-1)}).value, pi);
    EXPECT_EQ(applyFunction<T>(FunctionId::Atan2, {T(-1), T(0)}).value, -pi / 2);
    EXPECT_EQ(applyFunction<T>(FunctionId::Atan2, {T(0), T(2)}).value, T(0));
    EXPECT_EQ(applyFunction<T>(FunctionId::Atan2, {T(0), T(0)}).error.value_or(ErrorCode::Cancelled), ErrorCode::DomainError);
}

TEST(Catalogue, Atan2GivesAnAngle) {
    EXPECT_EQ(tree("atan2(1, 2)", AngleUnit::Degrees), "(* (atan2 1 2) (/ 180 pi))");
    EXPECT_EQ(tree("atan2(1)"), "error: atan2 takes 2 arguments");
}

TEST(Catalogue, Atan2JumpsAcrossTheNegativeAxis) {
    EXPECT_EQ(evaluate("atan2(0.1+0.2-0.3, -1)").error->code, ErrorCode::ArgumentNearJump);  // π or −π
    EXPECT_FALSE(evaluate("atan2(0.1+0.2-0.3, 1)").error);  // on the positive side it is smooth
    EXPECT_EQ(evaluate("atan2(0.1+0.2-0.3, 0.1+0.2-0.3)").error->code, ErrorCode::ArgumentNearEdge);  // the origin
    EXPECT_FALSE(evaluate("atan2(0, -1)").error);  // exactly on the axis: π
}

TEST(Catalogue, Atan2NearTheOriginWithOneExactZero) {
    EXPECT_EQ(evaluate("atan2(0.1+0.2-0.3, 0)").error->code, ErrorCode::ArgumentNearEdge);
    EXPECT_EQ(evaluate("atan2(0, 0.1+0.2-0.3)").error->code, ErrorCode::ArgumentNearEdge);
}

TYPED_TEST(CatalogueKernelTest, HypotenuseWithoutOverflow) {
    using T = TypeParam;
    test::expectWithinClaim<T>(FunctionId::Hypot, [](auto& rng) {
        return std::pair<T, T>{randomSign(rng, logUniform<T>(rng, -60, 60)), randomSign(rng, logUniform<T>(rng, -60, 60))};
    });
    const int top = maxExponent<T>() - 2;  // squares would overflow; the result does not
    test::expectWithinClaim<T>(FunctionId::Hypot, [top](auto& rng) {
        return std::pair<T, T>{logUniform<T>(rng, top - 4, top), logUniform<T>(rng, top - 4, top)};
    });
    EXPECT_EQ(applyFunction<T>(FunctionId::Hypot, {T(3), T(-4)}).value, T(5));
    EXPECT_EQ(applyFunction<T>(FunctionId::Hypot, {T(0), T(0)}).value, T(0));
    const T largest = (std::numeric_limits<T>::max)();
    EXPECT_EQ(applyFunction<T>(FunctionId::Hypot, {largest, largest}).error.value_or(ErrorCode::Cancelled), ErrorCode::Overflow);
}

TEST(Catalogue, HypotenuseInExactArithmetic) {
    EXPECT_EQ(inType("hypot(3/5, 4/5)", NumberType::Exact).exact->numerator, "1");
    EXPECT_EQ(inType("hypot(1, 1)", NumberType::Exact).error->code, ErrorCode::IrrationalResult);
}

TYPED_TEST(CatalogueKernelTest, CardinalSine) {
    using T = TypeParam;
    test::expectWithinClaim<T>(FunctionId::Sinc, [](auto& rng) { return std::pair<T, T>{randomSign(rng, logUniform<T>(rng, -40, 60)), T(0)}; });
    EXPECT_EQ(applyFunction<T>(FunctionId::Sinc, {T(0)}).value, T(1));
}

TEST(Catalogue, CardinalSineIgnoresTheAngleUnit) {
    EXPECT_EQ(tree("sinc(1)", AngleUnit::Degrees), "(sinc 1)");
}

TEST(Catalogue, SpanishNamesOfTheNewTrigonometry) {
    EXPECT_EQ(tree("cosec(1)"), tree("csc(1)"));
    EXPECT_EQ(tree("cotg(1)"), tree("cot(1)"));
    EXPECT_EQ(tree("cosech(1)"), "(csch 1)");
    EXPECT_EQ(tree("cotgh(1)"), tree("coth(1)"));
    EXPECT_EQ(tree("arccosec(2)"), tree("acsc(2)"));
    EXPECT_EQ(tree("arccotg(2)"), "(acot 2)");
    EXPECT_EQ(tree("arccosech(2)"), tree("acsch(2)"));
    EXPECT_EQ(tree("arccotgh(2)"), tree("acoth(2)"));
    EXPECT_EQ(evaluate("cosec(0)").error->message, "cosec is not defined for this argument");  // as written
}

TEST(Catalogue, PartAEndToEnd) {
    const Result r = evaluate("hypot(3, 4) + atan2(1, 1)*4 - sec(0)");  // 5 + pi - 1
    ASSERT_FALSE(r.error);
    EXPECT_TRUE(r.measurementReliable);
    for (NumberType t : {NumberType::Float, NumberType::Binary512}) {
        const Result each = inType("csch(1) + acoth(3) + sinc(2)", t);
        ASSERT_FALSE(each.error);
        EXPECT_TRUE(each.measurementReliable);
        EXPECT_NE(each.libraryError, "0");
    }
    for (const char* name : {"sec", "csc", "cot", "sech", "csch", "coth", "asec", "acsc", "acot",
                             "asech", "acsch", "acoth", "atan2", "hypot", "sinc"}) {
        bool listed = false;
        for (const FunctionDescription& f : functions()) listed = listed || f.name == name;
        EXPECT_TRUE(listed) << name;
    }
}

// A lowering's report is the written-out expression's, in every type (κ aside: a shared argument weighs its
// paths' signed slopes together, the written-out text apart).
TEST(Catalogue, ALoweringIsExactlyItsExpansion) {
    const std::pair<const char*, const char*> cases[] = {
        {"sec(0.7)", "1/cos(0.7)"},       {"csc(0.7)", "1/sin(0.7)"},       {"cot(0.7)", "1/tan(0.7)"},
        {"coth(0.7)", "1/tanh(0.7)"},     {"sech(0.7)", "2*exp(-abs(0.7))/(1+exp(-(2*abs(0.7))))"},
        {"asec(1.7)", "acos(1/1.7)"},     {"acsc(1.7)", "asin(1/1.7)"},     {"asech(0.4)", "acosh(1/0.4)"},
        {"acsch(1.7)", "asinh(1/1.7)"},   {"acoth(1.7)", "atanh(1/1.7)"},   {"log2(10)", "log(10, 2)"},
        {"exp2(0.3)", "2^0.3"},           {"exp10(0.3)", "10^0.3"},         {"sq(0.7)", "0.7²"},
        {"sqrtpi(0.7)", "sqrt(0.7*pi)"}};
    for (const TypeInfo& t : numberTypes())
        for (const auto& [lowered, written] : cases) {
            const Result a = inType(lowered, t.type);
            const Result b = inType(written, t.type);
            ASSERT_EQ(a.error.has_value(), b.error.has_value()) << t.label << ": " << lowered;
            if (a.error) continue;  // irrational in Exact
            EXPECT_EQ(a.value.digits, b.value.digits) << t.label << ": " << lowered;
            EXPECT_EQ(a.value.exponent10, b.value.exponent10) << t.label << ": " << lowered;
            EXPECT_EQ(a.bound, b.bound) << t.label << ": " << lowered;
            EXPECT_EQ(a.inputError, b.inputError) << t.label << ": " << lowered;
            EXPECT_EQ(a.roundingError, b.roundingError) << t.label << ": " << lowered;
            EXPECT_EQ(a.libraryError, b.libraryError) << t.label << ": " << lowered;
            EXPECT_EQ(a.measured, b.measured) << t.label << ": " << lowered;
            EXPECT_EQ(a.trustedDigits, b.trustedDigits) << t.label << ": " << lowered;
        }
}

template <class T>
class RoundingTest : public ::testing::Test {};
TYPED_TEST_SUITE(RoundingTest, test::AllTypes, test::TypeNames);

template <class T>
T applied(FunctionId id, std::vector<T> args) {
    const Applied<T> r = applyFunction<T>(id, args);
    EXPECT_FALSE(r.error) << static_cast<int>(id);
    return r.value;
}

TYPED_TEST(RoundingTest, Floor) {
    using T = TypeParam;
    EXPECT_EQ(applied<T>(FunctionId::Floor, {T(7) / T(4)}), T(1));
    EXPECT_EQ(applied<T>(FunctionId::Floor, {T(-7) / T(4)}), T(-2));
    EXPECT_EQ(applied<T>(FunctionId::Floor, {T(-3)}), T(-3));
    EXPECT_EQ(applied<T>(FunctionId::Floor, {T(0)}), T(0));
    EXPECT_EQ(applied<T>(FunctionId::Floor, {T(-1) / T(1024)}), T(-1));
}

TEST(Catalogue, CeilingIsMinusTheFloorOfMinusX) {
    EXPECT_EQ(tree("ceil(2.5)"), "(neg (floor (neg 2.5)))");
    for (const TypeInfo& t : numberTypes()) {
        EXPECT_EQ(inType("ceil(7/4)", t.type).error.has_value(), false) << t.label;
        const Result up = inType("ceil(-7/4)", t.type);
        EXPECT_TRUE(t.type == NumberType::Exact ? up.exact->numerator == "1" && up.exact->negative
                                                : up.value.digits == "1" && up.value.negative) << t.label;
    }
    EXPECT_EQ(evaluate("ceil(0.1*30)").error->message.rfind("ceil jumps", 0), 0u);
}

TEST(Catalogue, FloorRefusesAnArgumentWhoseErrorReachesAJump) {
    for (const char* text : {"floor((1 - 0.9)*10)", "floor(4.35*100)", "ceil(0.1*30)"}) {
        const Result r = evaluate(text);
        ASSERT_TRUE(r.error) << text;
        EXPECT_EQ(r.error->code, ErrorCode::ArgumentNearJump) << text;
        EXPECT_NE(r.error->message.find("its argument carries an error of up to"), std::string::npos);
    }
    Options allow;
    allow.allowUncertainDiscreteArguments = true;
    const Result anyway = evaluate("floor((1 - 0.9)*10)", allow);
    ASSERT_FALSE(anyway.error);
    EXPECT_EQ(anyway.value.digits, "0");
    EXPECT_FALSE(anyway.boundComplete);
    const Result far = evaluate("floor(0.1 + 0.2)");  // uncertain, but nowhere near 0 or 1
    ASSERT_FALSE(far.error);
    EXPECT_EQ(far.bound, "0");
    EXPECT_TRUE(far.boundComplete);
    EXPECT_EQ(inType("floor((1 - 9/10)*10)", NumberType::Exact).exact->numerator, "1");
}

TYPED_TEST(RoundingTest, TruncateTowardZero) {
    using T = TypeParam;
    EXPECT_EQ(applied<T>(FunctionId::Trunc, {T(7) / T(4)}), T(1));
    EXPECT_EQ(applied<T>(FunctionId::Trunc, {T(-7) / T(4)}), T(-1));
    EXPECT_EQ(applied<T>(FunctionId::Trunc, {T(-1) / T(4)}), T(0));
    EXPECT_EQ(applied<T>(FunctionId::Trunc, {T(5)}), T(5));
}

TEST(Catalogue, IntIsTruncAndDoesNotJumpAtZero) {
    EXPECT_EQ(tree("int(2.5)"), "(trunc 2.5)");
    EXPECT_EQ(tree("ent(2.5)"), "(trunc 2.5)");
    EXPECT_FALSE(evaluate("trunc(0.1*3 - 0.3)").error);  // near 0, where trunc is continuous
    EXPECT_EQ(evaluate("trunc(4.35*100)").error->code, ErrorCode::ArgumentNearJump);
    EXPECT_EQ(evaluate("int(4.35*100)").error->message.rfind("int jumps", 0), 0u);  // as written
}

TYPED_TEST(RoundingTest, RoundHalfAwayFromZero) {
    using T = TypeParam;
    EXPECT_EQ(applied<T>(FunctionId::Round, {T(5) / T(2)}), T(3));
    EXPECT_EQ(applied<T>(FunctionId::Round, {T(-5) / T(2)}), T(-3));
    EXPECT_EQ(applied<T>(FunctionId::Round, {T(7) / T(4)}), T(2));
    EXPECT_EQ(applied<T>(FunctionId::Round, {T(-1) / T(4)}), T(0));
    if constexpr (!isExact<T>) {
        // The largest T below 1/2 must round to 0 (a naive floor(x + 1/2) rounds it to 1).
        const T below = T(0.5) - ldexp(T(1), -precisionBits<T>() - 1);
        EXPECT_EQ(applied<T>(FunctionId::Round, {below}), T(0));
    }
}

TEST(Catalogue, RoundJumpsAtHalves) {
    EXPECT_FALSE(evaluate("round(0.25*10)").error);  // 2.5 exactly: a jump, but no error
    EXPECT_EQ(evaluate("round(0.1*25)").error->code, ErrorCode::ArgumentNearJump);
    EXPECT_FALSE(evaluate("round(0.1*30)").error);  // near 3, far from 2.5 and 3.5
}

TEST(Catalogue, FractionalPart) {
    EXPECT_EQ(tree("frac(2.5)"), "(- 2.5 (trunc 2.5))");
    for (const TypeInfo& t : numberTypes()) {
        const Result r = inType("frac(-9/4)", t.type);
        ASSERT_FALSE(r.error) << t.label;
        if (t.type == NumberType::Exact) EXPECT_EQ(r.exact->numerator, "1") << t.label;
        else EXPECT_EQ(r.value.digits, "25") << t.label;  // -0.25
        EXPECT_EQ(inType("frac(3)", t.type).bound, "0") << t.label;
    }
}

TEST(Catalogue, FractionalPartCarriesItsArgumentsError) {
    const Result r = evaluate("frac(0.1 + 0.2)");
    ASSERT_FALSE(r.error);
    EXPECT_EQ(r.bound, evaluate("0.1 + 0.2").bound);  // slope 1 away from the jumps
    EXPECT_EQ(evaluate("frac(0.1*30)").error->code, ErrorCode::ArgumentNearJump);
    EXPECT_EQ(evaluate("frac(0.1*30)").error->message.rfind("frac jumps", 0), 0u);
}

TYPED_TEST(RoundingTest, Sign) {
    using T = TypeParam;
    EXPECT_EQ(applied<T>(FunctionId::Sgn, {T(-5) / T(2)}), T(-1));
    EXPECT_EQ(applied<T>(FunctionId::Sgn, {T(0)}), T(0));
    EXPECT_EQ(applied<T>(FunctionId::Sgn, {T(1) / T(1000)}), T(1));
}

TEST(Catalogue, SignJumpsOnlyAtZero) {
    EXPECT_FALSE(evaluate("sgn(0.1 + 0.2)").error);
    EXPECT_EQ(evaluate("sgn(0.7 + 0.1 - 0.8)").error->code, ErrorCode::ArgumentNearJump);
    EXPECT_EQ(tree("signo(2)"), "(sgn 2)");
}

TYPED_TEST(RoundingTest, Clip) {
    using T = TypeParam;
    EXPECT_EQ(applied<T>(FunctionId::Clip, {T(5), T(0), T(2)}), T(2));
    EXPECT_EQ(applied<T>(FunctionId::Clip, {T(-5), T(0), T(2)}), T(0));
    EXPECT_EQ(applied<T>(FunctionId::Clip, {T(1), T(0), T(2)}), T(1));
    EXPECT_EQ(applyFunction<T>(FunctionId::Clip, {T(1), T(2), T(0)}).error.value_or(ErrorCode::Cancelled), ErrorCode::DomainError);
}

TEST(Catalogue, ClipCountsBothSidesOfAReachableCorner) {
    // 0.7 + 0.1 lands 1.1e-16 below lo = 0.8, within their errors (7.8e-17 + 4.4e-17): the clip may be lo's
    // value or x's, so the bound must hold both errors.
    const Result corner = evaluate("clip(0.7 + 0.1, 0.8, 1)");
    ASSERT_FALSE(corner.error);
    EXPECT_TRUE(corner.boundComplete);
    const Ruler x = Ruler(std::stod(evaluate("0.7 + 0.1").bound));
    const Ruler lo = Ruler(std::stod(evaluate("0.8").bound));
    EXPECT_GE(Ruler(std::stod(corner.bound)), (x + lo) * Ruler(0.95));  // two significant digits lose up to 5 %
    EXPECT_EQ(evaluate("clip(5, 0, 2)").bound, "0");
    EXPECT_EQ(evaluate("clip(0.3, 0.1 + 0.2 - 0.2, 1)").bound, evaluate("0.3").bound);  // far from lo: only x's error
}

TEST(Catalogue, NumeratorAndDenominatorOfTheStoredValue) {
    EXPECT_EQ(inType("numerator(-6/8)", NumberType::Exact).exact->numerator, "3");
    EXPECT_TRUE(inType("numerator(-6/8)", NumberType::Exact).exact->negative);
    EXPECT_EQ(inType("denominator(-6/8)", NumberType::Exact).exact->numerator, "4");
    // 0.1 is not exactly known in double: refused, then shown as the fraction double really holds.
    EXPECT_EQ(evaluate("numerator(0.1)").error->code, ErrorCode::UncertainDiscreteArgument);
    Options allow;
    allow.allowUncertainDiscreteArguments = true;
    EXPECT_EQ(evaluate("numerator(0.1)", allow).value.digits, "3602879701896397");
    const Result d = evaluate("denominator(0.1)", allow);
    EXPECT_EQ(d.value.digits, "36028797018963968");
    EXPECT_EQ(d.value.exponent10, 16);
    EXPECT_EQ(evaluate("denominator(0.5)").value.digits, "2");  // exact input: no refusal
    EXPECT_EQ(evaluate("denominator(1e-300)", allow).error->code, ErrorCode::Overflow);  // 2^1048 > max double
}

TEST(Catalogue, SpanishNamesOfRounding) {
    EXPECT_EQ(tree("redondeo(2.5)"), "(round 2.5)");
    EXPECT_EQ(tree("suelo(2.5)"), "(floor 2.5)");
    EXPECT_EQ(tree("techo(2.5)"), tree("ceil(2.5)"));  // ceil is a lowering: techo spells it
    EXPECT_EQ(evaluate("techo(0.1*30)").error->message.rfind("techo jumps", 0), 0u);
}

TEST(Catalogue, CeilingAndFractionalPartAreTheirExpansions) {
    for (const TypeInfo& t : numberTypes())
        for (const auto& [lowered, written] : {std::pair<const char*, const char*>{"ceil(2.3)", "-floor(-2.3)"},
                                               {"frac(2.3)", "2.3-trunc(2.3)"}}) {
            const Result a = inType(lowered, t.type);
            const Result b = inType(written, t.type);
            ASSERT_EQ(a.error.has_value(), b.error.has_value()) << t.label << ": " << lowered;
            if (a.error) continue;
            EXPECT_EQ(a.value.digits, b.value.digits) << t.label << ": " << lowered;
            EXPECT_EQ(a.bound, b.bound) << t.label << ": " << lowered;
        }
}
