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
