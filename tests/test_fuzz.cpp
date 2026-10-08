#include "engine.hpp"
#include "parser.hpp"
#include "test_support.hpp"

#include <calculate-core/calculate-core.hpp>

#include <cstdlib>
#include <random>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace calculate_core;
using namespace calculate_core::detail;

namespace {

// What the generator may write. Every fix that makes the engine honest for more of the language adds to it.
struct Vocabulary {
    std::vector<std::string> literals;
    std::vector<std::string> small;    // whole numbers and near-whole values for discrete arguments
    std::vector<std::string> prefix;   // written before an operand
    std::vector<std::string> infix;    // between two operands
    std::vector<std::string> postfix;  // after an operand
    std::vector<std::pair<std::string, int>> calls;     // name and argument count (-n: n to four)
    std::vector<std::pair<std::string, int>> discrete;  // calls whose arguments come from `small`
};

Vocabulary vocabulary() {
    Vocabulary v;
    v.literals = {"0",   "1",    "2",     "3",     "7",      "10",     "100",           "0.5",       "0.1",
                  "0.2", "0.3",  "0.7",   "1.1",   "2.5",    "1e-17",  "1e16",          "1e300",     "1e-300",
                  "(0.1+0.2-0.3)", "(1.1-0.1)", "(0.1*3)", "(0.7+0.1)", "-1", "(pi/2)",
                  "3.00000000000000001", "(-2)", "sum(1/x; 1; 5)", "product((1+x/10); 1; 4)", "sum(x^2; 3; 1)",
                  "sum(sum(y; 1; x; y); 1; 3)"};
    v.small = {"0", "1", "3", "5", "12", "20", "(0.1*30)", "2.5"};
    v.prefix = {"-", "√", "∛"};
    v.infix = {"+", "-", "*", "/", "^", "**", "·", " mod ", " rem ", " floormod "};
    v.postfix = {"%", "²", "³"};
    v.calls = {{"abs", 1},  {"exp", 1},  {"sin", 1},   {"cos", 1},  {"atan", 1}, {"sinh", 1},
               {"cosh", 1}, {"tanh", 1}, {"asinh", 1}, {"mean", -1}, {"varp", -1}, {"mod", 2},
               {"sqrt", 1}, {"cbrt", 1}, {"root", 2}, {"ln", 1}, {"log", 1}, {"log", 2}, {"tan", 1},
               {"asin", 1}, {"acos", 1}, {"acosh", 1}, {"atanh", 1}, {"var", -2}, {"stdev", -2}, {"stdevp", -1},
               {"median", -1}, {"log10", 1}, {"sen", 1}, {"arcsen", 1}, {"rem", 2}, {"floormod", 2}, {"arcsin", 1}, {"arsinh", 1}, {"log2", 1}, {"exp2", 1}, {"exp10", 1},
               {"sq", 1}, {"sqrtpi", 1}, {"sec", 1}, {"csc", 1}, {"cot", 1}, {"sech", 1}, {"csch", 1},
               {"coth", 1}, {"asec", 1}, {"acsc", 1}, {"acot", 1}, {"arcsec", 1}, {"asech", 1}, {"acsch", 1},
               {"acoth", 1}, {"atan2", 2}, {"hypot", 2}, {"sinc", 1}, {"floor", 1}, {"ceil", 1}, {"trunc", 1}, {"int", 1}, {"round", 1}, {"frac", 1}, {"sgn", 1}, {"signo", 1}};
    v.discrete = {{"gcd", 2}, {"lcm", 2}, {"nCr", 2}, {"nPr", 2}, {"numerator", 1}, {"denominator", 1}};
    v.calls.push_back({"clip", 3});
    return v;
}

class Generator {
public:
    Generator(Vocabulary v, unsigned seed) : v_(std::move(v)), rng_(seed) {}

    // A random expression with at most `depth` levels of nesting, written in the language.
    std::string expression(int depth) {
        if (depth == 0 || pick(4) == 0) return any(v_.literals);
        switch (pick(6)) {
        case 0:
        case 1: return "(" + expression(depth - 1) + any(v_.infix) + expression(depth - 1) + ")";
        case 2: return "(" + any(v_.prefix) + expression(depth - 1) + ")";
        case 3: return "(" + expression(depth - 1) + any(v_.postfix) + ")";
        case 4: return discreteCall();
        default: {
            const auto& [name, arity] = v_.calls[pick(v_.calls.size())];
            const int n = arity < 0 ? -arity + static_cast<int>(pick(static_cast<std::size_t>(5 + arity))) : arity;
            std::string s = name + "(";
            for (int i = 0; i < n; ++i) s += (i ? ", " : "") + expression(depth - 1);
            return s + ")";
        }
        }
    }

private:
    std::string discreteCall() {
        if (v_.discrete.empty() || pick(3) == 0) return "(" + any(v_.small) + ")!";
        const auto& [name, arity] = v_.discrete[pick(v_.discrete.size())];
        std::string s = name + "(";
        for (int i = 0; i < arity; ++i) s += (i ? ", " : "") + any(v_.small);
        return s + ")";
    }
    std::size_t pick(std::size_t n) { return static_cast<std::size_t>(rng_() % n); }
    const std::string& any(const std::vector<std::string>& list) { return list[pick(list.size())]; }

    Vocabulary v_;
    std::mt19937_64 rng_;
};

// Expressions per test; CALCULATE_FUZZ_SAMPLES overrides it for a long run at a checkpoint.
int samples(int normal) {
    const char* env = std::getenv("CALCULATE_FUZZ_SAMPLES");
    return env ? std::atoi(env) : normal;
}

// "" when the evaluation of `text` in T keeps the error contract; otherwise what went wrong.
template <class T>
std::string violation(const std::string& text, const Options& options) {
    using std::abs;
    const Parsed parsed = parse(text, options);
    if (parsed.error) return "a parse error: " + parsed.error->message;  // the generator writes valid expressions
    const Evaluation<T> ev = detail::evaluate<T>(parsed.ast, options);
    if (ev.error) return ev.error->begin <= ev.error->end && ev.error->end <= text.size() ? "" : "an error outside the text";
    if (!isFinite(ev.value)) return "a value that is not finite";
    const Report& r = ev.report;
    if (!r.boundComplete) return "";  // the user accepted an incomplete bound
    if (!checkExact(parsed.ast)) {    // the truth, where exact arithmetic gives it
        const Forward<Rational> exact = forward<Rational>(parsed.ast);
        if (exact.error) {
            const ErrorCode c = exact.error->code;
            if (c == ErrorCode::DivisionByZero || c == ErrorCode::DomainError)
                return "a value although the exact one is not defined: " + exact.error->message;
            return "";  // exact arithmetic cannot finish (too large, irrational): nothing to compare
        }
        const Ruler error = fromRational<Ruler>(abs(toRational(ev.value) - exact.values.back()));
        if (!test::covers(r.bound, error)) return "bound " + formatScientific(r.bound) + " < true error " + formatScientific(error);
        return "";
    }
    const Forward<Ruler> shadow = forward<Ruler>(parsed.ast);
    if (shadow.error && (shadow.error->code == ErrorCode::DivisionByZero || shadow.error->code == ErrorCode::DomainError))
        return "a value although the reference evaluation fails: " + shadow.error->message;
    if (r.measuredAvailable && r.reliable && !test::covers(r.bound, r.measured))
        return "bound " + formatScientific(r.bound) + " < measured " + formatScientific(r.measured);
    return "";
}

template <class T>
class FuzzTest : public ::testing::Test {};
TYPED_TEST_SUITE(FuzzTest, test::FloatingTypes, test::TypeNames);

}  // namespace

TYPED_TEST(FuzzTest, NoExpressionBeatsItsBound) {
    using T = TypeParam;
    const int count = samples(std::is_floating_point_v<T> ? 120 : 25);
    Options other;  // degrees, and the other reading of log and mod
    other.angle = AngleUnit::Degrees;
    other.conventions.log = Conventions::Log::Natural;
    other.conventions.mod = Conventions::Mod::Floored;
    other.conventions.percent = Conventions::Percent::OfValue;
    for (const Options& options : {Options(), other}) {
        Generator g(vocabulary(), 2026u + static_cast<unsigned>(options.angle));
        for (int i = 0; i < count; ++i) {
            const std::string text = g.expression(3);
            EXPECT_EQ(violation<T>(text, options), "") << text;
        }
    }
}

TEST(Fuzz, TheSameExpressionGivesTheSameResult) {
    Generator g(vocabulary(), 7);
    for (int i = 0; i < 40; ++i) {
        const std::string text = g.expression(3);
        const Result a = evaluate(text);
        const Result b = evaluate(text);
        EXPECT_EQ(a.error.has_value(), b.error.has_value()) << text;
        EXPECT_EQ(a.value.digits, b.value.digits) << text;
        EXPECT_EQ(a.bound, b.bound) << text;
        EXPECT_EQ(a.measured, b.measured) << text;
    }
}

TEST(Fuzz, ExactResultsAreTheRationalValue) {
    Generator g(vocabulary(), 11);
    Options exact;
    exact.type = NumberType::Exact;
    for (int i = 0; i < 60; ++i) {
        const std::string text = g.expression(3);
        const Result r = evaluate(text, exact);
        if (r.error) continue;
        EXPECT_EQ(r.bound, "0") << text;
        EXPECT_EQ(r.roundingError, "0") << text;
        EXPECT_TRUE(r.measurementReliable) << text;
    }
}

TEST(Fuzz, EveryCallOfTheVocabularyParses) {
    // Deterministic, unlike the property: each call with every argument count the generator may give it.
    const Vocabulary v = vocabulary();
    std::vector<std::pair<std::string, int>> calls = v.calls;
    calls.insert(calls.end(), v.discrete.begin(), v.discrete.end());
    for (const auto& [name, arity] : calls) {
        for (int n = arity < 0 ? -arity : arity; n <= (arity < 0 ? 4 : arity); ++n) {
            std::string text = name + "(";
            for (int i = 0; i < n; ++i) text += i ? ", 2" : "2";
            text += ")";
            EXPECT_FALSE(parse(text, AngleUnit::Radians).error) << text;
        }
    }
}

// Found by the long run: kernels that scale their argument lost the low bits of a subnormal one (asin through atan's
// halvings, tanh through expm1), so the result missed the claim's absolute floor.
TEST(Fuzz, OddFunctionsOfTinyArgumentsKeepTheirBound) {
    for (const char* text : {"asin((1e-17/1e300))", "tanh((1e-300/(20)!))", "atan(1e-320)", "sin(4e-320)",
                             "tan(4e-320)", "sinh(4e-320)", "asinh(4e-320)", "atanh(4e-320)", "asin(-3e-310)",
                             "atan(2e-309)", "tanh(-2e-309)", "asin(1e-6)", "tan(-3e-6)", "sinh(1e-7)", "atanh(2e-7)"})
        EXPECT_EQ(violation<double>(text, Options()), "") << text;
    for (const char* text : {"asin(1e-40)", "atan(1e-40)", "tanh(1e-40)", "asinh(-1e-40)", "atanh(3e-39)"})
        EXPECT_EQ(violation<float>(text, Options()), "") << text;
}

TEST(Fuzz, CommentsAndConversionsLeaveTheResultAlone) {
    Generator g(vocabulary(), 21);
    for (int i = 0; i < 40; ++i) {
        const std::string text = g.expression(3);
        const Result plain = evaluate(text);
        for (const char* suffix : {" # note", " to fraction", " to sci", " to mixed", " to percent", " to 1/3"}) {
            const Result r = evaluate(text + suffix);
            EXPECT_EQ(plain.error.has_value(), r.error.has_value()) << text << suffix;
            EXPECT_EQ(plain.value.digits, r.value.digits) << text << suffix;
            EXPECT_EQ(plain.bound, r.bound) << text << suffix;
        }
    }
}

// Found by the long run: csch's derivative was written cosh/sinh, which overflows (inf/inf) for a huge argument.
TEST(Fuzz, TheHyperbolicCosecantOfAHugeArgument) {
    for (const char* text : {"csch(1e16)", "csch(nPr(20, 20))", "csch(-1e300)"})
        EXPECT_EQ(violation<double>(text, Options()), "") << text;
}

// The special functions cost tens of milliseconds each in the shadows: a smaller run of their own.
TEST(Fuzz, SpecialFunctionsNeverBeatTheirBound) {
    Vocabulary v = vocabulary();
    v.calls = {{"gamma", 1}, {"lgamma", 1}, {"digamma", 1}, {"beta", 2}, {"erf", 1}, {"erfc", 1}, {"erfinv", 1},
               {"erfcinv", 1}, {"gammap", 2}, {"gammaq", 2}, {"igamma", 2}, {"gammainc", 2}, {"betainc", 3},
               {"betaincinv", 3}};
    v.literals = {"0.5", "1", "2", "2.5", "-2.5", "0.1", "0.3", "0.7", "(0.1+0.2-0.3)", "(0.1*30)", "1e-17", "30"};
    Generator g(v, 2040);
    Options options;
    for (int i = 0; i < samples(25); ++i) {
        const std::string text = g.expression(2);
        EXPECT_EQ(violation<double>(text, options), "") << text;
    }
}
