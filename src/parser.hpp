#pragma once

#include "ast.hpp"

#include <calculate-core/calculate-core.hpp>

#include <optional>
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

}  // namespace calculate_core::detail
