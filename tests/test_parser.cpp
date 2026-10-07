#include "accuracy.hpp"
#include "engine.hpp"
#include "parser.hpp"

#include <cstring>

using namespace calculate_core;
using namespace calculate_core::detail;

namespace {

std::vector<TokenKind> kinds(const Lexed& lexed) {
    std::vector<TokenKind> k;
    for (const Token& t : lexed.tokens) k.push_back(t.kind);
    return k;
}

}  // namespace

TEST(Lexer, NumbersNamesAndOperators) {
    const Lexed l = lex("12.5e-3 + x*(y)");
    ASSERT_FALSE(l.error);
    EXPECT_EQ(kinds(l), (std::vector<TokenKind>{TokenKind::Number, TokenKind::Plus, TokenKind::Identifier, TokenKind::Star,
                                                TokenKind::LeftParen, TokenKind::Identifier, TokenKind::RightParen, TokenKind::End}));
    EXPECT_EQ(l.tokens[0].text, "12.5e-3");
    EXPECT_EQ(l.tokens[0].span.end, 7u);
    EXPECT_EQ(l.tokens[2].span.begin, 10u);
    EXPECT_EQ(l.tokens.back().span.begin, 15u);
}

TEST(Lexer, AnExponentMayUseTheMinusSign) {
    // ×10ˣ followed by the − key types "5e−4" with U+2212, the sign the calculator shows.
    const Lexed l = lex("5e−4");
    ASSERT_FALSE(l.error);
    EXPECT_EQ(kinds(l), (std::vector<TokenKind>{TokenKind::Number, TokenKind::End}));
    EXPECT_EQ(l.tokens[0].text, "5e−4");
    const auto d = parseDecimal("5e−4");
    ASSERT_TRUE(d);
    EXPECT_EQ(d->significand, 5);
    EXPECT_EQ(d->exponent10, -4);
}

TEST(Lexer, AnExponentNeedsDigits) {
    const Lexed l = lex("2e");
    ASSERT_FALSE(l.error);
    EXPECT_EQ(kinds(l), (std::vector<TokenKind>{TokenKind::Number, TokenKind::Identifier, TokenKind::End}));
    EXPECT_EQ(l.tokens[0].text, "2");
}

TEST(Lexer, InvalidCharactersCoverTheWholeCodePoint) {
    const Lexed dollar = lex("2 $ 3");
    ASSERT_TRUE(dollar.error);
    EXPECT_EQ(dollar.error->code, ErrorCode::InvalidCharacter);
    EXPECT_EQ(dollar.error->begin, 2u);
    EXPECT_EQ(dollar.error->end, 3u);
    const Lexed euro = lex("€");
    ASSERT_TRUE(euro.error);
    EXPECT_EQ(euro.error->end, 3u);
    const Lexed dot = lex("1 + .");
    ASSERT_TRUE(dot.error);
    EXPECT_EQ(dot.error->code, ErrorCode::InvalidNumber);
}

TEST(Lexer, Utf8Aliases) {
    const Lexed l = lex("2×3÷4−5 π √ ∛ ² ³");
    ASSERT_FALSE(l.error);
    EXPECT_EQ(kinds(l), (std::vector<TokenKind>{TokenKind::Number, TokenKind::Star, TokenKind::Number, TokenKind::Slash,
                                                TokenKind::Number, TokenKind::Minus, TokenKind::Number, TokenKind::Pi,
                                                TokenKind::SquareRoot, TokenKind::CubeRoot, TokenKind::Squared,
                                                TokenKind::Cubed, TokenKind::End}));
    EXPECT_EQ(l.tokens[1].span.begin, 1u);
    EXPECT_EQ(l.tokens[1].span.end, 3u);  // × is two bytes
}

namespace {

// A readable form of a tree: (op arg ...), literals as written.
std::string sexpr(const Ast& ast, int i) {
    const Node& n = ast.nodes[i];
    if (n.function == FunctionId::Literal) return n.text;
    std::string op;
    switch (n.function) {
    case FunctionId::Add: op = "+"; break;
    case FunctionId::Subtract: op = "-"; break;
    case FunctionId::Multiply: op = "*"; break;
    case FunctionId::Divide: op = "/"; break;
    case FunctionId::Negate: op = "neg"; break;
    case FunctionId::Power: op = "^"; break;
    case FunctionId::Percent: op = "%"; break;
    case FunctionId::Square: op = "sq"; break;
    case FunctionId::Cube: op = "cube"; break;
    case FunctionId::Factorial: op = "!"; break;
    case FunctionId::LogBase: op = "logb"; break;
    default: op = std::string(functionInfo(n.function).name);
    }
    if (n.args.empty()) return op;
    std::string s = "(" + op;
    for (int a : n.args) s += " " + sexpr(ast, a);
    return s + ")";
}

std::string tree(std::string_view text, AngleUnit angle = AngleUnit::Radians, const Names& names = {}) {
    const Parsed p = parse(text, angle, names);
    if (p.error) return "error: " + p.error->message;
    EXPECT_TRUE(isPostOrder(p.ast)) << text;
    return sexpr(p.ast, p.ast.root());
}

}  // namespace

TEST(Parser, PrecedenceAndAssociativity) {
    EXPECT_EQ(tree("1+2*3"), "(+ 1 (* 2 3))");
    EXPECT_EQ(tree("(1+2)*3"), "(* (+ 1 2) 3)");
    EXPECT_EQ(tree("1-2-3"), "(- (- 1 2) 3)");
    EXPECT_EQ(tree("8/4/2"), "(/ (/ 8 4) 2)");
    EXPECT_EQ(tree("2^3^2"), "(^ 2 (^ 3 2))");
    EXPECT_EQ(tree("-2^2"), "(neg (^ 2 2))");
    EXPECT_EQ(tree("2^-3"), "(^ 2 (neg 3))");
    EXPECT_EQ(tree("-3!"), "(neg (! 3))");
    EXPECT_EQ(tree("2*-3"), "(* 2 (neg 3))");
    EXPECT_EQ(tree("--3"), "(neg (neg 3))");
    EXPECT_EQ(tree("+3"), "3");
}

TEST(Parser, PostfixAndPrefixOperators) {
    EXPECT_EQ(tree("50%"), "(% 50)");
    EXPECT_EQ(tree("200+10%"), "(+ 200 (% 10))");
    EXPECT_EQ(tree("3²+2³"), "(+ (sq 3) (cube 2))");
    EXPECT_EQ(tree("√4²"), "(sqrt (sq 4))");
    EXPECT_EQ(tree("√4^2"), "(^ (sqrt 4) 2)");
    EXPECT_EQ(tree("∛8"), "(cbrt 8)");
    EXPECT_EQ(tree("2^3!"), "(^ 2 (! 3))");
}

namespace {

Error parseError(std::string_view text) {
    const Parsed p = parse(text, AngleUnit::Radians);
    EXPECT_TRUE(p.error) << text;
    return p.error.value_or(Error{ErrorCode::Cancelled, "", 0, 0});
}

}  // namespace

TEST(Parser, CallsAndConstants) {
    EXPECT_EQ(tree("sin(1)"), "(sin 1)");
    EXPECT_EQ(tree("log(100)"), "(log 100)");
    EXPECT_EQ(tree("log(8, 2)"), "(logb 8 2)");
    EXPECT_EQ(tree("root(27, 3)"), "(root 27 3)");
    EXPECT_EQ(tree("nCr(5, 2)"), "(nCr 5 2)");
    EXPECT_EQ(tree("median(3, 1, 2)"), "(median 3 1 2)");
    EXPECT_EQ(tree("2*pi"), "(* 2 pi)");
    EXPECT_EQ(tree("π"), "pi");
    EXPECT_EQ(tree("e^2"), "(^ e 2)");
}

TEST(Parser, SpanishCalculatorNamesAreTheSameFunctions) {
    EXPECT_EQ(tree("sen(1)"), "(sin 1)");
    EXPECT_EQ(tree("arcsen(1)"), "(asin 1)");
    EXPECT_EQ(tree("arccos(1)"), "(acos 1)");
    EXPECT_EQ(tree("arctan(1)"), "(atan 1)");
    EXPECT_EQ(tree("senh(1)"), "(sinh 1)");
    EXPECT_EQ(tree("arcsenh(1)"), "(asinh 1)");
    EXPECT_EQ(tree("arccosh(2)"), "(acosh 2)");
    EXPECT_EQ(tree("arctanh(0)"), "(atanh 0)");
    EXPECT_EQ(tree("mcd(28, 35)"), "(gcd 28 35)");
    EXPECT_EQ(tree("mcm(9, 15)"), "(lcm 9 15)");
}

TEST(Parser, ImplicitMultiplicationIsRefused) {
    for (const char* text : {"2pi", "2π", "2(3)", "(1)(2)", "2 3", "2sin(1)"}) {
        const Error e = parseError(text);
        EXPECT_EQ(e.code, ErrorCode::MissingOperator) << text;
    }
    const Error e = parseError("2π");
    EXPECT_EQ(e.begin, 1u);
    EXPECT_EQ(e.end, 3u);
}

TEST(Parser, ErrorsPointAtTheirCause) {
    EXPECT_EQ(parseError("").code, ErrorCode::UnexpectedEnd);
    EXPECT_EQ(parseError("1+").code, ErrorCode::UnexpectedEnd);
    const Error open = parseError("(1+2");
    EXPECT_EQ(open.code, ErrorCode::MissingClosingParenthesis);
    EXPECT_EQ(open.begin, 0u);
    const Error close = parseError("1)");
    EXPECT_EQ(close.code, ErrorCode::UnexpectedToken);
    EXPECT_EQ(close.begin, 1u);
    EXPECT_EQ(close.end, 2u);
    const Error unknown = parseError("foo+1");
    EXPECT_EQ(unknown.code, ErrorCode::UnknownName);
    EXPECT_EQ(unknown.end, 3u);
    EXPECT_EQ(parseError("*3").code, ErrorCode::UnexpectedToken);
    EXPECT_EQ(parseError("sin 1").code, ErrorCode::UnexpectedToken);
    EXPECT_EQ(parseError("max(1)").code, ErrorCode::UnknownName);
    const Error arity = parseError("sin(1, 2)");
    EXPECT_EQ(arity.code, ErrorCode::WrongArgumentCount);
    EXPECT_EQ(arity.begin, 0u);
    EXPECT_EQ(arity.end, 9u);
    EXPECT_EQ(parseError("var(1)").code, ErrorCode::WrongArgumentCount);
    EXPECT_EQ(parseError("Ans+1").code, ErrorCode::UnknownName);
}

TEST(Parser, AnglesAreConvertedExplicitly) {
    EXPECT_EQ(tree("sin(90)", AngleUnit::Degrees), "(sin (* 90 (/ pi 180)))");
    EXPECT_EQ(tree("cos(100)", AngleUnit::Gradians), "(cos (* 100 (/ pi 200)))");
    EXPECT_EQ(tree("asin(1)", AngleUnit::Degrees), "(* (asin 1) (/ 180 pi))");
    EXPECT_EQ(tree("sin(1)", AngleUnit::Radians), "(sin 1)");
    EXPECT_EQ(tree("sinh(1)", AngleUnit::Degrees), "(sinh 1)");
}

TEST(Parser, StatisticsBecomeArithmeticOnSharedNodes) {
    EXPECT_EQ(tree("mean(1, 2, 3)"), "(/ (+ (+ 1 2) 3) 3)");
    EXPECT_EQ(tree("varp(1, 3)"), "(/ (+ (sq (- 1 (/ (+ 1 3) 2))) (sq (- 3 (/ (+ 1 3) 2)))) 2)");
    EXPECT_EQ(tree("var(1, 3)"), "(/ (+ (sq (- 1 (/ (+ 1 3) 2))) (sq (- 3 (/ (+ 1 3) 2)))) 1)");
    EXPECT_EQ(tree("stdev(1, 3)").substr(0, 6), "(sqrt ");
    const Parsed p = parse("varp(1, 3)", AngleUnit::Radians);
    ASSERT_FALSE(p.error);
    EXPECT_EQ(p.ast.nodes.size(), 12u);  // 1, 3, +, 2, mean, -, sq, -, sq, +, 2, /: the mean is shared
}

TEST(Parser, NamesExpandToTheirExpressions) {
    const Names names{{"Ans", "1+2"}};
    const Parsed p = parse("Ans*2", AngleUnit::Radians, names);
    ASSERT_FALSE(p.error);
    EXPECT_EQ(sexpr(p.ast, p.ast.root()), "(* (+ 1 2) 2)");
    EXPECT_EQ(p.expanded, "(1+2)*2");
    EXPECT_EQ(p.ast.nodes[0].span.begin, 0u);  // nodes from Ans point at "Ans"
    EXPECT_EQ(p.ast.nodes[0].span.end, 3u);
    EXPECT_EQ(parse("M - Ans", AngleUnit::Radians, {{"Ans", "2"}, {"M", "5"}}).expanded, "(5) - (2)");
}

TEST(Parser, ExactArithmeticRefusesTranscendentals) {
    const Parsed p = parse("1 + sin(2)", AngleUnit::Radians);
    const auto e = checkExact(p.ast);
    ASSERT_TRUE(e);
    EXPECT_EQ(e->code, ErrorCode::NotAvailableInExact);
    EXPECT_EQ(e->begin, 4u);
    EXPECT_EQ(e->end, 10u);
    EXPECT_NE(e->message.find("sin"), std::string::npos);
    EXPECT_TRUE(checkExact(parse("pi", AngleUnit::Radians).ast));
    EXPECT_FALSE(checkExact(parse("sqrt(4) + 2^(1/2) + 5!", AngleUnit::Radians).ast));
}

namespace {

// Text to report, the way the facade will do it: parse, check exactness, evaluate.
template <class T>
Evaluation<T> evaluateText(std::string_view text, AngleUnit angle = AngleUnit::Radians) {
    Evaluation<T> ev;
    const Parsed p = parse(text, angle);
    if (p.error) { ev.error = p.error; return ev; }
    if constexpr (isExact<T>) {
        if (auto e = checkExact(p.ast)) { ev.error = e; return ev; }
    }
    return evaluate<T>(p.ast);
}

}  // namespace

TEST(EndToEnd, SineOfOneEightyDegreesIsNotZeroAndSaysWhy) {
    const Evaluation<double> ev = evaluateText<double>("sin(180)", AngleUnit::Degrees);
    ASSERT_FALSE(ev.error);
    EXPECT_NE(ev.value, 0.0);
    EXPECT_LT(std::abs(ev.value), 1e-15);
    EXPECT_EQ(ev.report.measured, exactCast<Ruler>(std::abs(ev.value)));  // the true value is 0
    EXPECT_TRUE(test::covers(ev.report.bound, ev.report.measured));
    EXPECT_TRUE(ev.report.reliable);  // the true value is 0: the shadows only need to agree far below 1e-16
    EXPECT_GT(ev.report.input, 0);    // the error of pi
}

TEST(EndToEnd, Statistics) {
    EXPECT_EQ(evaluateText<double>("mean(1, 2, 3, 4)").value, 2.5);
    const Evaluation<double> population = evaluateText<double>("stdevp(2, 4, 4, 4, 5, 5, 7, 9)");
    EXPECT_EQ(population.value, 2.0);
    EXPECT_EQ(population.report.bound, 0);  // every step is exact
    EXPECT_EQ(evaluateText<Rational>("var(2, 4, 4, 4, 5, 5, 7, 9)").value, Rational(32, 7));
    EXPECT_EQ(evaluateText<Rational>("median(1/2, 1/3, 1/4)").value, Rational(1, 3));
}

TEST(EndToEnd, ExactArithmetic) {
    EXPECT_EQ(evaluateText<Rational>("1/3 + 1/6").value, Rational(1, 2));
    EXPECT_EQ(evaluateText<Rational>("sqrt(9/4)").value, Rational(3, 2));
    const Evaluation<Rational> irrational = evaluateText<Rational>("1 + sqrt(2)");
    ASSERT_TRUE(irrational.error);
    EXPECT_EQ(irrational.error->code, ErrorCode::IrrationalResult);
    EXPECT_EQ(irrational.error->begin, 4u);
    EXPECT_EQ(irrational.error->end, 11u);
    EXPECT_EQ(evaluateText<Rational>("sin(1)").error->code, ErrorCode::NotAvailableInExact);
}

TEST(EndToEnd, RumpFromText) {
    const char* rump = "333.75*33096^6 + 77617^2*(11*77617^2*33096^2 - 33096^6 - 121*33096^4 - 2)"
                       " + 5.5*33096^8 + 77617/(2*33096)";
    EXPECT_EQ(evaluateText<Rational>(rump).value, Rational(-54767, 66192));
    const Evaluation<double> ev = evaluateText<double>(rump);
    ASSERT_FALSE(ev.error);
    EXPECT_TRUE(ev.report.reliable);
    EXPECT_TRUE(test::covers(ev.report.bound, ev.report.measured));
    EXPECT_GT(ev.report.measured, Ruler(1));
}

TEST(EndToEnd, UncertainFactorialArgument) {
    const Evaluation<double> ev = evaluateText<double>("(0.1*30)!");
    ASSERT_TRUE(ev.error);
    EXPECT_EQ(ev.error->code, ErrorCode::UncertainDiscreteArgument);
    EXPECT_EQ(evaluateText<double>("(5+1)!").value, 720.0);
}

TEST(Parser, ACallWithoutItsClosingParenthesis) {
    const Error e = parseError("root(8, 2");
    EXPECT_EQ(e.code, ErrorCode::MissingClosingParenthesis);
    EXPECT_EQ(e.begin, 0u);
    EXPECT_EQ(e.end, 9u);
}

TEST(Parser, ParseReportsTheLexersError) {
    const Parsed p = parse("2 $ 3", AngleUnit::Radians);
    ASSERT_TRUE(p.error);
    EXPECT_EQ(p.error->code, ErrorCode::InvalidCharacter);
    EXPECT_EQ(p.error->begin, 2u);
}

TEST(Parser, ASemicolonSeparatesArgumentsLikeAComma) {
    EXPECT_EQ(tree("root(32; 5)"), tree("root(32, 5)"));
    EXPECT_EQ(tree("mean(1; 2, 3)"), tree("mean(1, 2, 3)"));
    EXPECT_EQ(tree("log(8; 2)"), tree("log(8, 2)"));
    EXPECT_EQ(parseError("sin(1; 2)").code, ErrorCode::WrongArgumentCount);
    EXPECT_EQ(parseError("1; 2").code, ErrorCode::UnexpectedToken);  // only inside a call
    const Lexed l = lex("f(1;2)");
    ASSERT_FALSE(l.error);
    EXPECT_EQ(l.tokens[3].kind, TokenKind::Comma);
    EXPECT_EQ(l.tokens[3].text, ";");
}

TEST(Lexer, StaysInsideItsView) {
    // The views end before text that would extend their last token: nothing past the end is read.
    // (The text alone would not show it: a token's text is clamped to the view, its span is not.)
    for (const char* text : {"123", "1e55", "sinx"}) {
        const std::string_view view(text, std::strlen(text) - 1);
        const Lexed l = lex(view);
        ASSERT_FALSE(l.error) << text;
        EXPECT_EQ(l.tokens[0].text, view) << text;
        EXPECT_EQ(l.tokens[0].span.end, view.size()) << text;
    }
    const Lexed dot = lex(std::string_view("1+.5", 3));
    ASSERT_TRUE(dot.error);
    EXPECT_EQ(dot.error->code, ErrorCode::InvalidNumber);
}

TEST(Parser, ArgumentCountsInTheirMessages) {
    EXPECT_EQ(parseError("sin(1, 2)").message, "sin takes 1 argument");
    EXPECT_EQ(parseError("root(8)").message, "root takes 2 arguments");
    EXPECT_EQ(parseError("mean()").message, "mean takes at least 1 argument");
}

TEST(Parser, OtherSpellingsNameTheFunctionItself) {
    EXPECT_EQ(tree("log10(100)"), "(log 100)");
    EXPECT_EQ(tree("sen(1)"), "(sin 1)");
    EXPECT_EQ(tree("mcd(4, 6)"), "(gcd 4 6)");
    EXPECT_EQ(parseError("log10(8, 2)").message, "log10 takes 1 argument");
    EXPECT_EQ(tree("log(8, 2)"), "(logb 8 2)");
    const Parsed p = parse("sen(1) + log10(2)", AngleUnit::Radians);
    ASSERT_FALSE(p.error);
    EXPECT_EQ(p.ast.nodes[1].written, "sen");  // 1, sin, 2, log10, +
    EXPECT_EQ(p.ast.nodes[3].written, "log10");
    EXPECT_EQ(p.ast.nodes[4].written, "");     // operators have no name
}

TEST(Parser, RemIsTheTruncatedRemainderAndModSpellsIt) {
    EXPECT_EQ(tree("rem(-7, 3)"), "(rem (neg 7) 3)");
    EXPECT_EQ(tree("mod(-7, 3)"), "(rem (neg 7) 3)");  // the default convention; 1.04 makes it a setting
}

namespace {

std::string treeWith(std::string_view text, const Options& options, const Names& names = {}) {
    const Parsed p = parse(text, options, names);
    if (p.error) return "error: " + p.error->message;
    return sexpr(p.ast, p.ast.root());
}

Options conventions(Conventions::Log log, Conventions::Mod mod) {
    Options o;
    o.conventions.log = log;
    o.conventions.mod = mod;
    return o;
}

}  // namespace

TEST(Parser, ConventionsDecideWhatLogAndModMean) {
    const Options base10 = conventions(Conventions::Log::Base10, Conventions::Mod::Truncated);
    const Options natural = conventions(Conventions::Log::Natural, Conventions::Mod::Floored);
    EXPECT_EQ(treeWith("log(100)", base10), "(log 100)");
    EXPECT_EQ(treeWith("log(100)", natural), "(ln 100)");
    EXPECT_EQ(treeWith("log(8, 2)", natural), "(logb 8 2)");  // two arguments: always the base
    EXPECT_EQ(treeWith("log10(100)", natural), "(log 100)");  // explicit names never change
    EXPECT_EQ(treeWith("mod(-7, 3)", base10), "(rem (neg 7) 3)");
    EXPECT_EQ(treeWith("mod(-7, 3)", natural), "(floormod (neg 7) 3)");
    EXPECT_EQ(treeWith("rem(-7, 3)", natural), "(rem (neg 7) 3)");
}

TEST(Parser, StoredTextsUseTheCanonicalSpelling) {
    const Options base10 = conventions(Conventions::Log::Base10, Conventions::Mod::Truncated);
    const Options natural = conventions(Conventions::Log::Natural, Conventions::Mod::Floored);
    EXPECT_EQ(parse("log(100) + mod(7, 3)", base10).expanded, "log10(100) + rem(7, 3)");
    EXPECT_EQ(parse("log(100) + mod(7, 3)", natural).expanded, "ln(100) + floormod(7, 3)");
    EXPECT_EQ(parse("log(8, 2) + sen(1)", natural).expanded, "log(8, 2) + sen(1)");  // nothing ambiguous
    EXPECT_EQ(parse("Ans*log(2)", natural, {{"Ans", "log10(5)"}}).expanded, "(log10(5))*ln(2)");
}

TEST(Parser, APercentageOfTheValueWhenTheConventionSaysSo) {
    Options of;
    of.conventions.percent = Conventions::Percent::OfValue;
    EXPECT_EQ(treeWith("200+10%", of), "(+ 200 (/ (* 200 10) 100))");
    EXPECT_EQ(treeWith("200-10%", of), "(- 200 (/ (* 200 10) 100))");
    EXPECT_EQ(treeWith("200+(10%)", of), "(+ 200 (% 10))");   // parentheses block it
    EXPECT_EQ(treeWith("200/10%", of), "(/ 200 (% 10))");     // only + and −
    EXPECT_EQ(treeWith("200+2*10%", of), "(+ 200 (* 2 (% 10)))");  // only a whole percentage operand
    EXPECT_EQ(treeWith("10%+200", of), "(+ (% 10) 200)");
    EXPECT_EQ(tree("200+10%"), "(+ 200 (% 10))");             // the default divides
    const Parsed p = parse("200+10%", of);
    ASSERT_FALSE(p.error);
    EXPECT_EQ(p.ast.nodes.size(), 6u);  // 200, 10, *, 100, /, +: 200 is shared and no % node is left behind
}

TEST(Parser, PercentagesAreWrittenOutInStoredTexts) {
    Options of;
    of.conventions.percent = Conventions::Percent::OfValue;
    EXPECT_EQ(parse("200+10%", of).expanded, "200+((200)×(10))÷100");
    EXPECT_EQ(parse("200+10%", AngleUnit::Radians).expanded, "200+(10%)");
    EXPECT_EQ(parse("1+2-5%", of).expanded, "1+2-((1+2)×(5))÷100");
    EXPECT_EQ(parse("Ans+10%", of, {{"Ans", "50"}}).expanded, "(50)+(((50))×(10))÷100");
    EXPECT_EQ(parse("200*10%", of).expanded, "200*10%");  // nothing ambiguous
}

TEST(Parser, AnInfixPercentSuggestsRem) {
    const Error e = parseError("3%2");
    EXPECT_EQ(e.code, ErrorCode::MissingOperator);
    EXPECT_NE(e.message.find("rem(3, 2)"), std::string::npos);
}

TEST(Lexer, ACommentEndsTheExpression) {
    const Lexed l = lex("(5×2)/2 # triangle area");
    ASSERT_FALSE(l.error);
    EXPECT_EQ(kinds(l), (std::vector<TokenKind>{TokenKind::LeftParen, TokenKind::Number, TokenKind::Star, TokenKind::Number,
                                                TokenKind::RightParen, TokenKind::Slash, TokenKind::Number, TokenKind::End}));
    EXPECT_EQ(l.tokens.back().span.begin, 9u);  // End sits where the comment starts
    ASSERT_TRUE(l.comment);
    EXPECT_EQ(l.comment->begin, 11u);  // "triangle area", without the spaces around it
    EXPECT_EQ(l.comment->end, 24u);
    const Lexed bare = lex("1 + 2 #");
    ASSERT_TRUE(bare.comment);
    EXPECT_EQ(bare.comment->begin, bare.comment->end);
    EXPECT_FALSE(lex("1 + 2").comment);
    const Lexed note = lex("# only $ € here");  // nothing after # is lexed
    ASSERT_FALSE(note.error);
    EXPECT_EQ(kinds(note), (std::vector<TokenKind>{TokenKind::End}));
}
