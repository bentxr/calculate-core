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
    std::vector<std::pair<std::string, int>> calls;     // name and argument count (-1: one to four)
    std::vector<std::pair<std::string, int>> discrete;  // calls whose arguments come from `small`
};

Vocabulary vocabulary() {
    Vocabulary v;
    v.literals = {"0",   "1",    "2",     "3",     "7",      "10",     "100",           "0.5",       "0.1",
                  "0.2", "0.3",  "0.7",   "1.1",   "2.5",    "1e-17",  "1e16",          "1e300",     "1e-300",
                  "(0.1+0.2-0.3)", "(1.1-0.1)", "(0.1*3)", "(0.7+0.1)"};
    v.small = {"0", "1", "3", "5", "12", "20", "(0.1*30)", "2.5"};
    v.prefix = {"-"};
    v.infix = {"+", "-", "*"};
    v.postfix = {"%", "²", "³"};
    v.calls = {{"abs", 1},  {"exp", 1},  {"sin", 1},   {"cos", 1},  {"atan", 1}, {"sinh", 1},
               {"cosh", 1}, {"tanh", 1}, {"asinh", 1}, {"mean", -1}, {"varp", -1}};
    v.discrete = {{"gcd", 2}, {"lcm", 2}, {"nCr", 2}, {"nPr", 2}};
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
            const int n = arity < 0 ? 1 + static_cast<int>(pick(4)) : arity;
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
    const Parsed parsed = parse(text, options.angle);
    if (parsed.error) return parsed.error->end <= text.size() ? "" : "a parse error outside the text";
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
    for (const AngleUnit angle : {AngleUnit::Radians, AngleUnit::Degrees}) {
        Generator g(vocabulary(), 2026u + static_cast<unsigned>(angle));
        Options options;
        options.angle = angle;
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
