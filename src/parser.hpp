#pragma once

#include "ast.hpp"

#include <calculate-core/calculate-core.hpp>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace calculate_core::detail {

enum class TokenKind {
    Number, Identifier, Plus, Minus, Star, Slash, Caret, LeftParen, RightParen, Comma,
    Bang, Percent, Squared, Cubed, SquareRoot, CubeRoot, Pi, Assign, PlusMinus, PerMille, PerMyriad, End
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

// Names of numbers, read as exact literals: the long scale (10^9 = a thousand millions) in every language.
struct NumberName {
    std::string_view name;
    std::string_view literal;  // the number, as a literal: "1e12"
    std::string_view title;    // English: "one billion (10^12)"
};

inline constexpr std::array<NumberName, 34> numberNames{{
    {"million", "1e6", "one million (10^6)"},
    {"milliard", "1e9", "one milliard (10^9)"},
    {"billion", "1e12", "one billion (10^12)"},
    {"billiard", "1e15", "one billiard (10^15)"},
    {"trillion", "1e18", "one trillion (10^18)"},
    {"trilliard", "1e21", "one trilliard (10^21)"},
    {"quadrillion", "1e24", "one quadrillion (10^24)"},
    {"quintillion", "1e30", "one quintillion (10^30)"},
    {"sextillion", "1e36", "one sextillion (10^36)"},
    {"septillion", "1e42", "one septillion (10^42)"},
    {"octillion", "1e48", "one octillion (10^48)"},
    {"nonillion", "1e54", "one nonillion (10^54)"},
    {"decillion", "1e60", "one decillion (10^60)"},
    {"googol", "1e100", "one googol (10^100)"},
    {"lakh", "1e5", "one lakh (10^5)"},
    {"crore", "1e7", "one crore (10^7)"},
    {"dozen", "12", "one dozen (12)"},
    {"gross", "144", "one gross (144)"},
    {"score", "20", "one score (20)"},
    {"ppm", "1e-6", "parts per million"},
    {"pcm", "1e-5", "per cent mille (10^-5)"},
    {"millón", "1e6", "one million (10^6) (Spanish name)"},
    {"millardo", "1e9", "one milliard (10^9) (Spanish name)"},
    {"billón", "1e12", "one billion (10^12) (Spanish name)"},
    {"trillón", "1e18", "one trillion (10^18) (Spanish name)"},
    {"cuatrillón", "1e24", "one quadrillion (10^24) (Spanish name)"},
    {"quintillón", "1e30", "one quintillion (10^30) (Spanish name)"},
    {"sextillón", "1e36", "one sextillion (10^36) (Spanish name)"},
    {"septillón", "1e42", "one septillion (10^42) (Spanish name)"},
    {"octillón", "1e48", "one octillion (10^48) (Spanish name)"},
    {"nonillón", "1e54", "one nonillion (10^54) (Spanish name)"},
    {"decillón", "1e60", "one decillion (10^60) (Spanish name)"},
    {"docena", "12", "one dozen (12) (Spanish name)"},
    {"gruesa", "144", "one gross (144) (Spanish name)"},
}};

// The Spanish names written without their accent, and the name they stand for.
inline constexpr std::array<std::pair<std::string_view, std::string_view>, 10> numberSpellings{{
    {"millon", "millón"}, {"billon", "billón"}, {"trillon", "trillón"}, {"cuatrillon", "cuatrillón"},
    {"quintillon", "quintillón"}, {"sextillon", "sextillón"}, {"septillon", "septillón"}, {"octillon", "octillón"},
    {"nonillon", "nonillón"}, {"decillon", "decillón"},
}};

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

// The other spellings the parser accepts for a function (sin: sen) and for a lowering by its name (csc: cosec).
std::vector<std::string> otherSpellings(FunctionId id);
std::vector<std::string> otherSpellings(std::string_view lowering);

// The first node the Exact type cannot evaluate, as an error.
std::optional<Error> checkExact(const Ast& ast);

}  // namespace calculate_core::detail
