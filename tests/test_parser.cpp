#include "accuracy.hpp"
#include "engine.hpp"
#include "parser.hpp"

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
