#pragma once

#include "ast.hpp"

#include <calculate-core/calculate-core.hpp>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace calculate_core::detail {

enum class TokenKind {
    Number, Identifier, Plus, Minus, Star, Slash, Caret, LeftParen, RightParen, Comma,
    Bang, Percent, Squared, Cubed, SquareRoot, CubeRoot, Pi, End
};

struct Token {
    TokenKind kind;
    std::string_view text;  // a view into the source
    Span span;
};

struct Lexed {
    std::optional<Error> error;
    std::vector<Token> tokens;  // always ends with End when there is no error
};

// Locale-free; ASCII plus the UTF-8 aliases × ÷ − π √ ∛ ² ³.
Lexed lex(std::string_view source);

// Named expressions (Ans, M): the name is replaced by its text, in parentheses.
using Names = std::map<std::string, std::string, std::less<>>;

struct Parsed {
    std::optional<Error> error;
    Ast ast;
    std::string expanded;  // the source with every name replaced by "(" + its text + ")"
};

Parsed parse(std::string_view source, AngleUnit angle, const Names& names = {});

// The first node the Exact type cannot evaluate, as an error.
std::optional<Error> checkExact(const Ast& ast);

}  // namespace calculate_core::detail
