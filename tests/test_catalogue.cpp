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
