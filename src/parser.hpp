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
    Bang, Percent, Squared, Cubed, SquareRoot, CubeRoot, Pi, Assign, End
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
    std::optional<Span> keyword;  // `to`, `->` or `→`, when a target follows the expression
    Span target;                  // the text after the keyword, up to any '#', without surrounding spaces
};

// Locale-free; ASCII plus the UTF-8 aliases × ÷ − π √ ∛ ² ³. `;` is a second spelling of the argument separator.
// A '#' starts a comment that runs to the end. `to`, `->` or `→` ends the expression: what follows, up to a '#', is
// a conversion target and is not lexed.
Lexed lex(std::string_view source);

// Named expressions (Ans, M): the name is replaced by its text, in parentheses.
using Names = std::map<std::string, std::string, std::less<>>;

// "to <name> <argument>" after an expression: the form its result should be shown in.
struct TargetText {
    std::string name;      // the first word: "fraction"
    std::string argument;  // the rest, trimmed: "32" in "to base 32"; usually empty
    Span span;             // name and argument in the source
};

struct Parsed {
    std::optional<Error> error;
    Ast ast;
    std::string expanded;  // the expression only (no comment), every name replaced by "(" + its text + ")"
    std::string comment;   // the text after '#', trimmed; "" when none
    bool commentOnly = false;  // the source holds nothing but a comment
    std::optional<TargetText> target;
    std::vector<Warning> warnings;  // the parser's notes (an empty range)
    std::string assigned;           // the variable's name when the source was "name := expression"
    std::string reading;            // the canonical reading: every operation in parentheses, "(2 ^ (3 ^ 2))"
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
