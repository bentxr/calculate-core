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
    std::optional<Span> comment;  // the text after '#', without surrounding spaces; absent when there is no '#'
};

// Locale-free; ASCII plus the UTF-8 aliases × ÷ − π √ ∛ ² ³. `;` is a second spelling of the argument separator.
// A '#' starts a comment that runs to the end.
Lexed lex(std::string_view source);

// Named expressions (Ans, M): the name is replaced by its text, in parentheses.
using Names = std::map<std::string, std::string, std::less<>>;

struct Parsed {
    std::optional<Error> error;
    Ast ast;
    std::string expanded;  // the expression only (no comment), every name replaced by "(" + its text + ")"
    std::string comment;   // the text after '#', trimmed; "" when none
    bool commentOnly = false;  // the source holds nothing but a comment
};

Parsed parse(std::string_view source, const Options& options, const Names& names = {});
inline Parsed parse(std::string_view source, AngleUnit angle, const Names& names = {}) {
    Options o;
    o.angle = angle;
    return parse(source, o, names);
}

// The first node the Exact type cannot evaluate, as an error.
std::optional<Error> checkExact(const Ast& ast);

}  // namespace calculate_core::detail
